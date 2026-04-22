/**
 * Application.cpp
 */

#include "app/Application.h"
#include "app/LocalBridge.h"
#include "app/MessageFilter.h"
#include "app/NetworkInit.h"
#include "mqtt/MqttClient.h"
#include "hardware/BarrierManager.h"
#include "hardware/VehicleSensorManager.h"
#include "hardware/NV9Manager.h"
#include "hardware/AudioPlayer.h"
#include "hardware/DisplayMonitor.h"
#include "ui/MainWindow.h"

#include <QDebug>
#include <QTimer>

Application::Application(Config& cfg,
                          const QString& config_path,
                          QObject* parent)
    : QObject(parent), cfg_(cfg), config_path_(config_path)
{
    has_ui_stack_ = (cfg_.device.type != DeviceType::EntryOnly);

    mqtt_      = new MqttClient(cfg_, this);
    sensor_    = new VehicleSensorManager(cfg_, this);
    barrier_   = new BarrierManager(cfg_, sensor_, this);
    audio_     = new AudioPlayer(cfg_, this);
    cmd_queue_ = new CommandQueue(this);

    // NV9 + MainWindow only exist for UI-bearing profiles. EntryOnly runs
    // headless: no cash reader, no screen. Foreign inbound messages
    // (vehicle_info, payment_success, display_msg) are dropped further down.
    if (has_ui_stack_) {
        nv9_    = new NV9Manager(cfg_, this);
        window_ = new MainWindow(cfg_);
    } else {
        qInfo() << "[App] entry_only profile — skipping NV9 + MainWindow";
    }

    // Local IPC bridge to wifi-provisioning. Always on — provisioning
    // remains reachable even if the cloud MQTT is down.
    LocalBridge::BridgeConfig lbcfg;
    local_bridge_ = new LocalBridge(lbcfg, &cfg_, config_path_, this);
    connect(local_bridge_, &LocalBridge::configUpdated, this, [this]() {
        qInfo() << "[App] config updated from provisioning — "
                   "relevant changes apply on next restart";
        // Outright live-reloading device_type would require tearing down
        // half the stack; safer to ask systemd to bounce the service.
    });

    // MQTT → App
    connect(mqtt_, &MqttClient::connected,       this, &Application::onMqttConnected);
    connect(mqtt_, &MqttClient::disconnected,    this, &Application::onMqttDisconnected);
    connect(mqtt_, &MqttClient::connectionError, this, &Application::onMqttError);
    connect(mqtt_, &MqttClient::inboundMessage,  this, &Application::onInboundMessage);

    // Command queue → App (handles dequeued commands in order)
    connect(cmd_queue_, &CommandQueue::commandReady,
            this, &Application::runCommand);

    // Hardware → App
    connect(barrier_, &BarrierManager::statusChanged,
            this, &Application::onBarrierStatusChanged);
    connect(barrier_, &BarrierManager::autoClosed,
            this, &Application::onBarrierAutoClosed);
    connect(barrier_, &BarrierManager::vehiclePassed,
            this, &Application::onVehiclePassed);
    if (nv9_) {
        connect(nv9_, &NV9Manager::cashInserted,
                this, &Application::onNV9CashInserted);
        connect(nv9_, &NV9Manager::errorOccurred,
                this, &Application::onNV9Error);
    }

    // UI → App
    if (window_) {
        connect(window_, &MainWindow::callSupportRequested,
                this, &Application::onCallSupport);
    }

    // Display hot-plug monitor (Linux only, no-op if status file not found)
    display_monitor_ = new DisplayMonitor(this);
    connect(display_monitor_, &DisplayMonitor::displayConnected,
            this, &Application::onDisplayConnected);
    connect(display_monitor_, &DisplayMonitor::displayDisconnected,
            this, &Application::onDisplayDisconnected);

    // Payment timer — gives the user at least 5 minutes to finish an
    // hourly payment. On fire we publish `session_abandoned` so the
    // server releases the session, and we prompt the user to press the
    // support button so a guard can take cash and open the gate.
    payment_timer_ = new QTimer(this);
    payment_timer_->setSingleShot(true);
    payment_timer_->setInterval(cfg_.ui.payment_timeout_seconds * 1000);
    connect(payment_timer_, &QTimer::timeout, this, &Application::onIdleTimeout);

    uptime_.start();
}

Application::~Application() {
    if (local_bridge_) local_bridge_->stop();
    mqtt_->disconnectGraceful();
    barrier_->closeAll();
    if (sensor_) sensor_->shutdown();
    if (nv9_)    nv9_->stop();
    delete window_;
}

void Application::start() {
    qInfo() << "[App] Starting as device=" << cfg_.device.id
            << "type=" << deviceTypeStr(cfg_.device.type);

    if (window_) {
        if (cfg_.ui.fullscreen)
            window_->showFullScreen();
        else
            window_->show();

        window_->showIdle();
        window_->setStatusMessage("Đang kiểm tra kết nối mạng...");
        window_->setMqttStatus(false);
    }

    // Probe internet + detect country. proceedStartup() is called once the
    // probe resolves (or times out after 5 s) so startup never blocks.
    auto* net_init = new NetworkInit(5000, this);
    connect(net_init, &NetworkInit::done, this,
            [this, net_init](bool connected, const QString& cc, const QString& tz) {
        net_init->deleteLater();

        if (connected) {
            qInfo() << "[App] Network OK — country=" << cc << "tz=" << tz;
            if (window_) window_->setStatusMessage("Kết nối mạng: OK");
        } else {
            qWarning() << "[App] Không có internet hoặc geolocation thất bại";
            if (window_) window_->setStatusMessage(
                "Không có mạng — tiếp tục với cài đặt mặc định");
        }

        proceedStartup();
    });
    net_init->run();
}

void Application::proceedStartup() {
    // Start display monitor before hardware init so we know display state early.
    display_monitor_->start(2000);
    display_available_ = display_monitor_->isConnected()
                      || display_monitor_->statusFilePath().isEmpty();

    sensor_->init();
    barrier_->init();

    // Hand the barrier driver to the provisioning bridge now that it's open.
    if (local_bridge_) {
        local_bridge_->setBarrierDriver(barrier_->driver());
        local_bridge_->start();
    }

    if (nv9_ && cfg_.device.has_cash_reader)
        nv9_->start();

    mqtt_->connectToServer();
}

// ── MQTT Handlers ─────────────────────────────────────────────────────────────

void Application::onMqttConnected() {
    qInfo() << "[App] MQTT connected";
    if (window_) {
        window_->setMqttStatus(true);
        window_->setStatusMessage("Kết nối server: OK");
    }
    audio_->play("open_entry"); // startup chime
}

void Application::onMqttDisconnected() {
    qWarning() << "[App] MQTT disconnected";
    if (window_) {
        window_->setMqttStatus(false);
        window_->setStatusMessage("Mất kết nối server...");
    }
}

void Application::onMqttError(const QString& reason) {
    qCritical() << "[App] MQTT error:" << reason;
    if (window_) window_->setStatusMessage("Lỗi kết nối: " + reason);
}

void Application::onInboundMessage(const InboundMessage& msg) {
    // Filter foreign messages based on deployment profile. The same gate is
    // exercised directly by parking_selftest so changes to the matrix can't
    // drift silently.
    if (!parking::shouldDispatch(cfg_.device.type, msg.type)) {
        qInfo() << "[App] drop foreign msg: type=" << static_cast<int>(msg.type)
                << "mode=" << deviceTypeStr(cfg_.device.type);
        return;
    }

    switch (msg.type) {
        case InboundMsgType::VehicleInfo:
            handleVehicleInfo(*msg.vehicle_info);
            break;
        case InboundMsgType::OpenCommand:
            handleOpenCommand(*msg.open_cmd);
            break;
        case InboundMsgType::QrPaymentConfirmed:
            handleQrConfirmed(*msg.qr_confirmed);
            break;
        case InboundMsgType::PaymentSuccess:
            handlePaymentSuccess(*msg.payment_success);
            break;
        case InboundMsgType::DisplayMessage:
            handleDisplayMsg(*msg.display_msg);
            break;
        case InboundMsgType::Reconnect:
            qInfo() << "[App] Reconnect requested by server"
                    << "(reason=" << msg.reconnect->reason << ")";
            // Re-announce ourselves so the server can rebuild its view.
            mqtt_->publishConnect();
            break;
        default:
            qWarning() << "[App] Unknown inbound message type";
    }
}

// ── Message Handlers ──────────────────────────────────────────────────────────

void Application::handleVehicleInfo(const VehicleInfo& vi) {
    if (!window_) {
        qInfo() << "[App] entry_only — dropping vehicle_info for plate="
                << vi.plate;
        return;
    }
    qInfo() << "[App] VehicleInfo: plate=" << vi.plate
            << "ticket=" << static_cast<int>(vi.ticket_type)
            << "amount=" << vi.amount_due;

    // Save session
    current_session_id_     = vi.session_id;
    current_barrier_id_     = vi.barrier_id;
    current_plate_          = vi.plate;
    current_scenario_tag_   = vi.scenario_tag;
    current_amount_due_     = vi.amount_due;
    current_amount_paid_    = 0;
    current_payment_method_ = {};
    current_ticket_type_    = vi.ticket_type;
    session_active_         = true;
    payment_stale_          = false;

    if (vi.ticket_type == TicketType::Monthly) {
        // Monthly: display info + auto open
        window_->showExitMonthly(vi);
        audio_->play("monthly_valid");

        if (vi.open) {
            const auto* b  = cfg_.findBarrier(vi.barrier_id);
            const int  secs = b ? b->auto_close_seconds : 10;
            enqueueOpen(vi.barrier_id, secs, OpenReason::MonthlyTicket);
        }
    } else {
        // Hourly: display payment screen
        window_->showExitHourly(vi);

        if (vi.payment_methods.contains("cash") && nv9_ && cfg_.device.has_cash_reader) {
            nv9_->enableAccept();
            audio_->play("insert_money");
        }

        payment_timer_->start();

        if (cfg_.self_test.enabled && vi.scenario_tag == "cash_auto_pay"
            && nv9_ && cfg_.device.has_cash_reader && vi.amount_due > 0) {
            qInfo() << "[SelfTest] scheduling simulated cash payment"
                    << vi.amount_due << "for session" << vi.session_id;
            QTimer::singleShot(cfg_.self_test.auto_cash_insert_delay_ms, this,
                               [this, amount = vi.amount_due, session = vi.session_id]() {
                if (current_session_id_ != session || !session_active_) return;
                nv9_->simulateCash(amount);
            });
        }
    }
}

void Application::handleOpenCommand(const OpenCommand& cmd) {
    qInfo() << "[App] OpenCommand: barrier=" << cmd.barrier_id
            << "reason=" << openReasonStr(cmd.reason);

    // ── Validate barrier_id is wired to this device.
    const auto* bc = cfg_.findBarrier(cmd.barrier_id);
    if (!bc) {
        qWarning() << "[App] reject open_cmd: barrier" << cmd.barrier_id
                   << "not in this device's config";
        return;
    }

    // ── Refuse unknown reasons outright. Forces server to use one of the
    //    enum values; otherwise a misrouted/garbled payload could open a
    //    gate at the wrong time.
    if (cmd.reason == OpenReason::Unknown) {
        qWarning() << "[App] reject open_cmd: unknown reason on barrier"
                   << cmd.barrier_id;
        return;
    }

    // ── Refuse if the reason implies a direction that doesn't match the
    //    barrier's role. ManualOverride is direction-neutral and always
    //    allowed (guard / dashboard physically forced the gate).
    const BarrierDirection implied = openReasonImpliesDirection(cmd.reason);
    if (implied != BarrierDirection::Unknown && implied != bc->direction) {
        qWarning() << "[App] reject open_cmd:" << openReasonStr(cmd.reason)
                   << "implies" << directionStr(implied)
                   << "but barrier" << cmd.barrier_id
                   << "is" << directionStr(bc->direction);
        return;
    }

    CommandQueue::Command c;
    c.kind           = CommandQueue::Kind::Open;
    c.barrier_id     = cmd.barrier_id;
    c.session_id     = cmd.session_id;
    c.reason         = cmd.reason;
    c.auto_close_sec = cmd.auto_close_seconds;
    c.play_sound     = cmd.play_sound;
    c.sound_file     = cmd.sound_file;
    cmd_queue_->enqueue(std::move(c));
}

void Application::handleQrConfirmed(const QrPaymentConfirmed& qr) {
    if (!window_) return;   // entry_only — filter already ignored this type
    qInfo() << "[App] QR Payment confirmed: amount=" << qr.amount_paid
            << "app=" << qr.payment_app;

    current_amount_paid_    = qr.amount_paid;
    current_payment_method_ = qr.payment_app;
    window_->updatePaymentReceived(qr.amount_paid, 0);
    audio_->play("payment_success");
    // Server follows with payment_success which triggers the barrier open.
}

void Application::handlePaymentSuccess(const PaymentSuccess& ps) {
    qInfo() << "[App] PaymentSuccess: plate=" << ps.plate
            << "status=" << ps.status
            << "barriers=" << ps.barrier_ids;

    // Lock out the session — stop accepting cash and cancel the payment timer.
    payment_timer_->stop();
    if (nv9_) nv9_->disableAccept();
    session_active_ = false;
    payment_stale_  = false;

    // Play the audio only if qr_confirmed hasn't already triggered it.
    if (current_amount_paid_ == 0)
        audio_->play("payment_success");

    // Prefer the plate the server sends (may be more complete than our cache).
    if (!ps.plate.isEmpty())
        current_plate_ = ps.plate;

    // Only open barriers that belong to this device — skip any foreign IDs
    // the server may have included for a different device in the session.
    for (const QString& bid : ps.barrier_ids) {
        const auto* b = cfg_.findBarrier(bid);
        if (!b) {
            qWarning() << "[App] payment_success: barrier" << bid
                       << "not in this device's config — skipping";
            continue;
        }
        enqueueOpen(bid, b->auto_close_seconds, OpenReason::PaymentConfirmed);
    }

    // Server may override the idle countdown via auto_idle_seconds; fall back
    // to the device config default when the field is absent (== 0).
    const int countdown = (ps.auto_idle_seconds > 0)
                        ? ps.auto_idle_seconds
                        : cfg_.ui.payment_success_countdown_seconds;

    // Use the amount and payment method the device tracked locally —
    // the server's payment_success no longer echoes those fields.
    if (window_) window_->showPaymentSuccess(current_plate_, current_amount_paid_,
                                              current_payment_method_, countdown);
}

void Application::handleDisplayMsg(const DisplayMessage& dm) {
    if (!window_) return;   // entry_only — filter already dropped this
    qInfo() << "[App] Display message:" << dm.message;
    window_->showDisplayMessage(dm);
    if (dm.play_sound && !dm.sound_text.isEmpty())
        audio_->playTTS(dm.sound_text);
}

// ── Command queue ────────────────────────────────────────────────────────────

void Application::enqueueOpen(const QString& barrier_id, int auto_close_sec,
                               OpenReason reason, bool play_sound,
                               const QString& sound_file)
{
    CommandQueue::Command c;
    c.kind           = CommandQueue::Kind::Open;
    c.barrier_id     = barrier_id;
    c.session_id     = current_session_id_;
    c.reason         = reason;
    c.auto_close_sec = auto_close_sec;
    c.play_sound     = play_sound;
    c.sound_file     = sound_file;
    cmd_queue_->enqueue(std::move(c));
}

void Application::runCommand(const CommandQueue::Command& cmd) {
    switch (cmd.kind) {
        case CommandQueue::Kind::Open:
            if (cmd.play_sound)
                audio_->play(cmd.sound_file.isEmpty() ? "open_exit" : cmd.sound_file);
            if (window_)
                window_->showBarrierOpening(cmd.barrier_id, current_plate_);
            barrier_->requestOpen(cmd.barrier_id, cmd.auto_close_sec);
            break;

        case CommandQueue::Kind::Close:
            barrier_->requestClose(cmd.barrier_id);
            break;
    }
    // Free the queue for the next command.
    cmd_queue_->ack();
}

// ── Hardware Handlers ─────────────────────────────────────────────────────────

void Application::onBarrierStatusChanged(const QString& barrier_id,
                                          const QString& position) {
    qInfo() << "[App] Barrier" << barrier_id << "position:" << position;
    if (window_) window_->setBarrierStatus(barrier_id, position);
}

void Application::onBarrierAutoClosed(const QString& barrier_id) {
    qInfo() << "[App] Barrier auto-closed:" << barrier_id
            << "— resetting session";
    session_active_ = false;
    payment_stale_  = false;
    if (nv9_) nv9_->disableAccept();
    payment_timer_->stop();
    current_scenario_tag_.clear();
    if (window_) window_->showIdle();
}

void Application::onVehiclePassed(const QString& barrier_id) {
    qInfo() << "[App] Vehicle cleared sensor at" << barrier_id;
}

void Application::onNV9CashInserted(quint32 amount) {
    if (!nv9_) return;  // defensive — signal shouldn't fire without NV9
    if (!session_active_) {
        qWarning() << "[App] Cash inserted but no session active — rejecting";
        nv9_->rejectNote();
        return;
    }
    if (payment_stale_) {
        qWarning() << "[App] Cash inserted on abandoned session — rejecting."
                      " User must pay guard in person.";
        nv9_->rejectNote();
        return;
    }

    current_amount_paid_    += amount;
    current_payment_method_  = "Tiền mặt";
    quint32 remaining = current_amount_paid_ >= current_amount_due_
                      ? 0
                      : current_amount_due_ - current_amount_paid_;

    qInfo() << "[App] Cash: paid=" << current_amount_paid_
            << "due=" << current_amount_due_
            << "remaining=" << remaining;

    if (window_) window_->updatePaymentReceived(current_amount_paid_, remaining);

    if (current_amount_paid_ >= current_amount_due_) {
        // Full payment received
        nv9_->disableAccept();
        audio_->play("payment_success");
        payment_timer_->stop();

        // Notify server
        PaymentMessage pm;
        pm.device_id    = cfg_.device.id;
        pm.barrier_id   = current_barrier_id_;
        pm.session_id   = current_session_id_;
        pm.amount_due   = current_amount_due_;
        pm.amount_paid  = current_amount_paid_;
        pm.amount_change= current_amount_paid_ - current_amount_due_;
        pm.method       = PaymentMethod::Cash;
        mqtt_->publishPayment(pm);
        // Server will respond with open_cmd
    }
}

void Application::onNV9Error(const QString& error) {
    qCritical() << "[App] NV9 error:" << error;
    if (window_) window_->setStatusMessage("Lỗi đầu đọc tiền: " + error);
    audio_->play("error");
}

// ── UI Handlers ───────────────────────────────────────────────────────────────

void Application::onCallSupport() {
    qInfo() << "[App] Call support requested"
            << "session=" << current_session_id_
            << "stale=" << payment_stale_;

    // Publish to ops so a guard can come take cash and hit the manual
    // override. We tag the reason so the dashboard can prioritize
    // payment-timeout cases over generic "I need help" presses.
    const QString reason = payment_stale_ ? "payment_timeout"
                                          : "user_request";
    if (session_active_) {
        mqtt_->publishSupportRequest(current_session_id_,
                                      current_barrier_id_,
                                      current_plate_,
                                      reason);
    } else {
        // No active session — still let ops know someone pressed the
        // button so they can check the lane.
        mqtt_->publishSupportRequest({}, {}, {}, "idle_press");
    }

    audio_->play("error");
    if (window_) window_->showNotification(
        "Đã gửi yêu cầu hỗ trợ. Bảo vệ sẽ đến trong giây lát.\n"
        "Vui lòng nộp tiền mặt cho bảo vệ để mở cần.",
        10 * 1000);
}

void Application::onIdleTimeout() {
    if (!session_active_) return;

    if (current_ticket_type_ != TicketType::Hourly) {
        // Non-hourly sessions were never supposed to park on this screen;
        // fall back to the legacy reset path.
        qWarning() << "[App] Non-hourly session timed out — resetting";
        session_active_ = false;
        if (nv9_)    nv9_->disableAccept();
        if (window_) window_->showIdle();
        return;
    }

    if (payment_stale_) {
        // Already escalated once — don't spam the server.
        qInfo() << "[App] payment timeout re-fired, still waiting on guard";
        return;
    }

    qWarning() << "[App] Payment timed out on session"
               << current_session_id_
               << "— escalating to server + guard";

    payment_stale_ = true;
    if (nv9_) nv9_->disableAccept();   // stop accepting cash into the validator

    // Tell the server so it stops waiting for this plate.
    mqtt_->publishSessionAbandoned(current_session_id_,
                                    current_barrier_id_,
                                    current_plate_,
                                    current_amount_due_,
                                    current_amount_paid_);

    // Surface the instruction prominently — user must press the hardware
    // GỌI HỖ TRỢ button mounted on the kiosk to summon a guard, who will
    // then trigger a ManualOverride OpenCommand via the ops dashboard.
    audio_->play("error");
    if (window_) window_->showNotification(
        "Quá thời gian thanh toán.\n"
        "Vui lòng nhấn nút GỌI HỖ TRỢ (phím cứng) để gọi bảo vệ.",
        30 * 1000);

    if (cfg_.self_test.enabled
        && cfg_.self_test.auto_call_support
        && current_scenario_tag_ == "timeout_manual_override") {
        qInfo() << "[SelfTest] scheduling support request for timed-out session"
                << current_session_id_;
        QTimer::singleShot(cfg_.self_test.auto_call_support_delay_ms, this, [this]() {
            if (!payment_stale_) return;
            onCallSupport();
        });
    }
}

void Application::onDisplayConnected() {
    if (display_available_ || !window_) return;
    display_available_ = true;
    qInfo() << "[App] Display reconnected — restoring window";

    // Re-show the window. Qt's eglfs/linuxfb will re-acquire the framebuffer.
    if (cfg_.ui.fullscreen)
        window_->showFullScreen();
    else
        window_->show();

    // Restore the correct screen based on current session state.
    if (session_active_) {
        window_->setMqttStatus(mqtt_->isConnected());
    } else {
        window_->showIdle();
        window_->setMqttStatus(mqtt_->isConnected());
    }
}

void Application::onDisplayDisconnected() {
    if (!display_available_ || !window_) return;
    display_available_ = false;
    qWarning() << "[App] Display disconnected — hiding window, backend continues";

    // Hide (not close) so backend keeps running and the window object stays
    // valid. No signals, no session teardown — everything continues normally.
    window_->hide();
}
