#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QPointer>
#include <QTextStream>
#include <QTimer>

#include <mosquitto.h>

#include <atomic>

namespace {

QString nowIso() {
    return QDateTime::currentDateTime().toString(Qt::ISODate);
}

QString stamp() {
    return QDateTime::currentDateTime().toString("yyyy-MM-dd HH:mm:ss.zzz");
}

void logLine(const QString& tag, const QString& msg) {
    QTextStream ts(stdout);
    ts << "[" << stamp() << "] [" << tag << "] " << msg << Qt::endl;
}

void logBanner(const QString& tag, const QString& title) {
    const QString line(72, '=');
    logLine(tag, line);
    logLine(tag, "========== " + title + " ==========");
    logLine(tag, line);
}

struct BrokerConfig {
    QString host = "localhost";
    int port = 1883;
    QString username = "parking";
    QString password = "parking123";
};

BrokerConfig loadBrokerConfig(const QString& path) {
    BrokerConfig cfg;
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        logLine("CFG", "cannot open config, using broker defaults: " + path);
        return cfg;
    }

    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        logLine("CFG", "invalid JSON, using broker defaults: " + err.errorString());
        return cfg;
    }

    const auto mqtt = doc.object().value("mqtt").toObject();
    cfg.host = mqtt.value("host").toString(cfg.host);
    cfg.port = mqtt.value("port").toInt(cfg.port);
    cfg.username = mqtt.value("username").toString(cfg.username);
    cfg.password = mqtt.value("password").toString(cfg.password);
    return cfg;
}

QString topicCmd(const QString& deviceId) {
    return "parking/v1/server/" + deviceId + "/cmd";
}

QString topicBroadcast() {
    return "parking/v1/broadcast/cmd";
}

QString topicClientPattern() {
    return "parking/v1/client/+/#";
}

QString makeSessionId(const QString& prefix) {
    return prefix + "_" + QDateTime::currentDateTime().toString("yyyyMMdd_hhmmss_zzz");
}

struct SessionInfo {
    QString deviceId;
    QString plate;
    QStringList barrierIds;
    int amountDue = 0;
    QString scenarioTag;
};

struct DeviceInfo {
    QString id;
    QStringList barrierIds;
    QString connectedAt;
    QString lastSeen;
};

} // namespace

class ParkingTestServer final : public QObject {
    Q_OBJECT

public:
    ParkingTestServer(const BrokerConfig& cfg, bool autorun, QObject* parent = nullptr)
        : QObject(parent), broker_(cfg), autorun_(autorun) {}

    ~ParkingTestServer() override {
        stop();
    }

    bool start() {
        if (mosq_) return true;
        if (gMosqRefs_.fetch_add(1) == 0)
            mosquitto_lib_init();

        mosq_ = mosquitto_new("parking_test_server_cpp", true, this);
        if (!mosq_) {
            logLine("MQTT", "mosquitto_new failed");
            return false;
        }

        mosquitto_username_pw_set(mosq_,
            broker_.username.toUtf8().constData(),
            broker_.password.toUtf8().constData());
        mosquitto_connect_callback_set(mosq_, &ParkingTestServer::cbConnect);
        mosquitto_disconnect_callback_set(mosq_, &ParkingTestServer::cbDisconnect);
        mosquitto_message_callback_set(mosq_, &ParkingTestServer::cbMessage);

        logLine("MQTT", QString("connecting to %1:%2").arg(broker_.host).arg(broker_.port));
        const int rc = mosquitto_connect_async(
            mosq_, broker_.host.toUtf8().constData(), broker_.port, 60);
        if (rc != MOSQ_ERR_SUCCESS) {
            logLine("MQTT", "connect_async failed: " + QString::fromUtf8(mosquitto_strerror(rc)));
            stop();
            return false;
        }

        mosquitto_loop_start(mosq_);
        return true;
    }

    void stop() {
        if (!mosq_) return;
        mosquitto_loop_stop(mosq_, true);
        mosquitto_destroy(mosq_);
        mosq_ = nullptr;
        if (gMosqRefs_.fetch_sub(1) == 1)
            mosquitto_lib_cleanup();
    }

private:
    enum class CaseId {
        Reconnect,
        HourlyQr,
        HourlyCash,
        TimeoutManualOverride,
        Monthly,
        DisplayMessage,
        Done
    };

    static inline std::atomic<int> gMosqRefs_{0};

    static void cbConnect(mosquitto*, void* self, int rc) {
        auto* me = static_cast<ParkingTestServer*>(self);
        QMetaObject::invokeMethod(me, [me, rc]() { me->handleConnect(rc); },
                                  Qt::QueuedConnection);
    }

    static void cbDisconnect(mosquitto*, void* self, int rc) {
        auto* me = static_cast<ParkingTestServer*>(self);
        QMetaObject::invokeMethod(me, [me, rc]() { me->handleDisconnect(rc); },
                                  Qt::QueuedConnection);
    }

    static void cbMessage(mosquitto*, void* self, const mosquitto_message* msg) {
        auto* me = static_cast<ParkingTestServer*>(self);
        const QByteArray topic = QByteArray(msg->topic);
        const QByteArray payload(static_cast<const char*>(msg->payload), msg->payloadlen);
        QMetaObject::invokeMethod(me, [me, topic, payload]() {
            me->handleMessage(topic, payload);
        }, Qt::QueuedConnection);
    }

    void handleConnect(int rc) {
        if (rc != 0) {
            logLine("MQTT", "connection refused: " + QString::number(rc));
            return;
        }
        logLine("MQTT", "connected");
        const int subRc = mosquitto_subscribe(mosq_, nullptr,
                                              topicClientPattern().toUtf8().constData(), 1);
        if (subRc != MOSQ_ERR_SUCCESS) {
            logLine("MQTT", "subscribe failed: " + QString::fromUtf8(mosquitto_strerror(subRc)));
            return;
        }
        logLine("MQTT", "subscribed: " + topicClientPattern());
        publishBroadcastReconnect("server_restart");
    }

    void handleDisconnect(int rc) {
        logLine("MQTT", "disconnected rc=" + QString::number(rc));
    }

    void handleMessage(const QByteArray& topicBytes, const QByteArray& payloadBytes) {
        QJsonParseError err;
        const auto doc = QJsonDocument::fromJson(payloadBytes, &err);
        if (err.error != QJsonParseError::NoError || !doc.isObject()) {
            logLine("RX", "invalid JSON on " + QString::fromUtf8(topicBytes));
            return;
        }

        const QString topic = QString::fromUtf8(topicBytes);
        const auto root = doc.object();
        const QString type = root.value("type").toString();
        const QStringList parts = topic.split('/');
        if (parts.size() < 5) return;
        const QString deviceId = parts.value(3);

        logLine("RX", topic + " type=" + type);

        if (type == "connect") {
            DeviceInfo dev;
            dev.id = deviceId;
            dev.connectedAt = stamp();
            dev.lastSeen = stamp();
            for (const auto& it : root.value("barriers").toArray()) {
                const QString bid = it.toObject().value("barrier_id").toString();
                if (!bid.isEmpty())
                    dev.barrierIds.append(bid);
            }
            devices_[deviceId] = dev;
            if (activeDeviceId_.isEmpty())
                activeDeviceId_ = deviceId;

            if (awaitingReconnectAck_ && deviceId == activeDeviceId_) {
                awaitingReconnectAck_ = false;
                passCase("device re-announced after reconnect broadcast");
            } else if (autorun_ && !suiteStarted_ && deviceId == activeDeviceId_) {
                suiteStarted_ = true;
                logLine("TEST", "device ready: " + activeDeviceId_);
                QTimer::singleShot(1000, this, [this]() { runNextCase(); });
            }
            return;
        }

        if (devices_.contains(deviceId))
            devices_[deviceId].lastSeen = stamp();

        if (type == "ping")
            return;

        if (type == "payment") {
            currentCaseSawPayment_ = true;
            const QString sessionId = root.value("session_id").toString();
            const auto session = sessions_.value(sessionId);
            logLine("PAYMENT", QString("session=%1 amount=%2").arg(
                sessionId, QString::number(root.value("amount_paid").toInt())));
            if (currentCase_ == CaseId::HourlyCash && sessionId == currentSessionId_) {
                sendPaymentSuccess(session.deviceId, session.barrierIds, sessionId, session.plate, 5);
                QTimer::singleShot(5000, this, [this]() { passCase("cash flow completed"); });
            }
            return;
        }

        if (type == "session_abandoned") {
            currentCaseSawAbandoned_ = true;
            logLine("EVENT", "session_abandoned session=" + root.value("session_id").toString());
            return;
        }

        if (type == "support_request") {
            currentCaseSawSupport_ = true;
            logLine("EVENT", "support_request reason=" + root.value("reason").toString());
            if (currentCase_ == CaseId::TimeoutManualOverride
                && root.value("session_id").toString() == currentSessionId_) {
                QTimer::singleShot(700, this, [this]() {
                    const auto session = sessions_.value(currentSessionId_);
                    if (!session.deviceId.isEmpty() && !session.barrierIds.isEmpty()) {
                        sendOpenCmd(session.deviceId, session.barrierIds.first(),
                                    currentSessionId_, "manual_override", 6);
                    }
                });
                QTimer::singleShot(6500, this, [this]() {
                    if (currentCaseSawAbandoned_ && currentCaseSawSupport_)
                        passCase("timeout escalated and manual override sent");
                });
            }
            return;
        }
    }

    void runNextCase() {
        resetCaseFlags();
        currentCase_ = nextCase_;

        switch (currentCase_) {
        case CaseId::Reconnect:
            startCase("Reconnect broadcast");
            ensureReadyDevice();
            awaitingReconnectAck_ = true;
            publishBroadcastReconnect("self_test_case");
            armCaseTimeout(6000, "device did not reconnect");
            nextCase_ = CaseId::HourlyQr;
            break;

        case CaseId::HourlyQr: {
            startCase("Hourly QR flow");
            if (!ensureReadyDevice()) return;
            currentSessionId_ = sendVehicleInfo(activeDeviceId_, pickBarriers(1), "51K-999.88",
                                                "hourly", 12000, "qr_auto_pay");
            armCaseTimeout(12000, "QR flow timed out");
            QTimer::singleShot(1200, this, [this]() {
                const auto session = sessions_.value(currentSessionId_);
                sendQrConfirmed(session.deviceId, session.barrierIds, currentSessionId_, session.amountDue);
            });
            QTimer::singleShot(7000, this, [this]() { passCase("QR payment path completed"); });
            nextCase_ = CaseId::HourlyCash;
            break;
        }

        case CaseId::HourlyCash:
            startCase("Hourly cash flow");
            if (!ensureReadyDevice()) return;
            currentSessionId_ = sendVehicleInfo(activeDeviceId_, pickBarriers(1), "51K-111.22",
                                                "hourly", 20000, "cash_auto_pay");
            armCaseTimeout(12000, "client did not publish payment");
            nextCase_ = CaseId::TimeoutManualOverride;
            break;

        case CaseId::TimeoutManualOverride:
            startCase("Timeout + support + manual override");
            if (!ensureReadyDevice()) return;
            currentSessionId_ = sendVehicleInfo(activeDeviceId_, pickBarriers(1), "51K-333.44",
                                                "hourly", 15000, "timeout_manual_override");
            armCaseTimeout(20000, "timeout/manual-override flow timed out");
            nextCase_ = CaseId::Monthly;
            break;

        case CaseId::Monthly:
            startCase("Monthly auto-open flow");
            if (!ensureReadyDevice()) return;
            currentSessionId_ = sendVehicleInfo(activeDeviceId_, pickBarriers(2), "30A-888.99",
                                                "monthly", 0, "monthly_auto_open");
            armCaseTimeout(10000, "monthly flow timed out");
            QTimer::singleShot(6000, this, [this]() { passCase("monthly command sequence sent"); });
            nextCase_ = CaseId::DisplayMessage;
            break;

        case CaseId::DisplayMessage:
            startCase("Display message");
            if (!ensureReadyDevice()) return;
            sendDisplayMessage(activeDeviceId_, pickBarriers(1).value(0),
                               "Self-test complete. Device is healthy.", 5);
            armCaseTimeout(8000, "display message case timed out");
            QTimer::singleShot(6000, this, [this]() {
                passCase("display message displayed long enough", 500);
            });
            nextCase_ = CaseId::Done;
            break;

        case CaseId::Done:
            logBanner("TEST", QString("SUITE FINISHED: %1/%2 PASSED")
                      .arg(passCount_).arg(caseCount_));
            QCoreApplication::quit();
            break;
        }
    }

    void startCase(const QString& name) {
        ++caseCount_;
        currentCaseName_ = name;
        caseActive_ = true;
        logBanner("TEST", QString("CASE %1/%2 :: %3")
                  .arg(caseCount_)
                  .arg(totalCaseCount_)
                  .arg(name));
    }

    void passCase(const QString& reason, int nextDelayMs = 2500) {
        if (!caseActive_) return;
        caseActive_ = false;
        if (caseTimeout_) caseTimeout_->stop();
        ++passCount_;
        logBanner("PASS", currentCaseName_ + " :: " + reason);
        QTimer::singleShot(nextDelayMs, this, [this]() { runNextCase(); });
    }

    void failCase(const QString& reason) {
        if (!caseActive_) return;
        caseActive_ = false;
        if (caseTimeout_) caseTimeout_->stop();
        logBanner("FAIL", currentCaseName_ + " :: " + reason);
        QCoreApplication::exit(2);
    }

    void armCaseTimeout(int timeoutMs, const QString& reason) {
        if (!caseTimeout_) {
            caseTimeout_ = new QTimer(this);
            caseTimeout_->setSingleShot(true);
            connect(caseTimeout_, &QTimer::timeout, this, [this]() {
                failCase(caseTimeoutReason_);
            });
        }
        caseTimeoutReason_ = reason;
        caseTimeout_->start(timeoutMs);
    }

    bool ensureReadyDevice() {
        if (!devices_.contains(activeDeviceId_)) {
            failCase("no connected device");
            return false;
        }
        if (devices_[activeDeviceId_].barrierIds.isEmpty()) {
            failCase("device reported no barriers");
            return false;
        }
        return true;
    }

    QStringList pickBarriers(int count) const {
        const auto list = devices_.value(activeDeviceId_).barrierIds;
        if (list.isEmpty()) return {};
        if (count <= 1 || list.size() == 1)
            return {list.first()};
        QStringList out;
        for (int i = 0; i < list.size() && i < count; ++i)
            out.append(list.at(i));
        return out;
    }

    void resetCaseFlags() {
        currentCaseSawPayment_ = false;
        currentCaseSawAbandoned_ = false;
        currentCaseSawSupport_ = false;
        awaitingReconnectAck_ = false;
        currentSessionId_.clear();
    }

    void publishJson(const QString& topic, const QJsonObject& obj, int qos = 1, bool retain = false) {
        if (!mosq_) return;
        const QByteArray payload = QJsonDocument(obj).toJson(QJsonDocument::Compact);
        mosquitto_publish(mosq_, nullptr, topic.toUtf8().constData(),
                          payload.size(), payload.constData(), qos, retain);
        logLine("TX", topic + " type=" + obj.value("type").toString());
    }

    void publishBroadcastReconnect(const QString& reason) {
        QJsonObject root;
        root["type"] = "reconnect";
        root["reason"] = reason;
        root["timestamp"] = nowIso();
        publishJson(topicBroadcast(), root, 1, false);
    }

    QString sendVehicleInfo(const QString& deviceId, const QStringList& barrierIds,
                            const QString& plate, const QString& ticketType,
                            int amountDue, const QString& scenarioTag) {
        if (barrierIds.isEmpty())
            return {};

        const QString sessionId = makeSessionId(scenarioTag.isEmpty() ? "sess" : scenarioTag);
        const auto now = QDateTime::currentDateTime();
        const auto entryTime = now.addSecs(-137 * 60);

        QJsonObject root;
        root["type"] = "vehicle_info";
        root["session_id"] = sessionId;
        root["scenario_tag"] = scenarioTag;
        root["barrier_id"] = barrierIds.first();
        root["direction"] = "exit";
        root["plate"] = plate;
        root["ticket_type"] = ticketType;
        root["entry_time"] = entryTime.toString(Qt::ISODate);
        root["exit_time"] = now.toString(Qt::ISODate);
        root["duration_minutes"] = 137;
        root["amount_due"] = amountDue;
        root["currency"] = "VND";

        if (ticketType == "hourly") {
            QJsonArray methods;
            methods.append("cash");
            methods.append("qr");
            root["payment_methods"] = methods;
            root["qr_url"] = QString("SPAY|%1|%2|%3").arg(sessionId, plate).arg(amountDue);
            root["qr_expires_at"] = now.addSecs(300).toString(Qt::ISODate);
        } else if (ticketType == "monthly") {
            root["open"] = true;
            root["owner_name"] = "Nguyen Van A";
            root["valid_until"] = now.date().addMonths(1).toString(Qt::ISODate);
            root["amount_due"] = 0;
        }

        SessionInfo session;
        session.deviceId = deviceId;
        session.plate = plate;
        session.barrierIds = barrierIds;
        session.amountDue = amountDue;
        session.scenarioTag = scenarioTag;
        sessions_[sessionId] = session;

        publishJson(topicCmd(deviceId), root);

        if (ticketType == "monthly" && barrierIds.size() > 1) {
            for (int i = 1; i < barrierIds.size(); ++i) {
                QTimer::singleShot(200 * i, this, [this, deviceId, barrierId = barrierIds.at(i), sessionId]() {
                    sendOpenCmd(deviceId, barrierId, sessionId, "monthly_ticket", 8);
                });
            }
        }

        return sessionId;
    }

    void sendOpenCmd(const QString& deviceId, const QString& barrierId,
                     const QString& sessionId, const QString& reason, int autoCloseSeconds) {
        QJsonObject root;
        root["type"] = "open_cmd";
        root["barrier_id"] = barrierId;
        root["session_id"] = sessionId;
        root["reason"] = reason;
        root["play_sound"] = true;
        root["sound_file"] = "open_exit.wav";
        root["auto_close_seconds"] = autoCloseSeconds;
        publishJson(topicCmd(deviceId), root);
    }

    void sendQrConfirmed(const QString& deviceId, const QStringList& barrierIds,
                         const QString& sessionId, int amount) {
        if (barrierIds.isEmpty()) return;
        QJsonObject root;
        root["type"] = "qr_payment_confirmed";
        root["session_id"] = sessionId;
        root["barrier_id"] = barrierIds.first();
        root["amount_paid"] = amount;
        root["payment_app"] = "SmartParkTest";
        root["transaction_id"] = "TXN-" + QDateTime::currentDateTime().toString("hhmmsszzz");
        root["timestamp"] = nowIso();
        publishJson(topicCmd(deviceId), root);

        QTimer::singleShot(350, this, [this, deviceId, barrierIds, sessionId]() {
            const auto session = sessions_.value(sessionId);
            sendPaymentSuccess(deviceId, barrierIds, sessionId, session.plate, 5);
        });
    }

    void sendPaymentSuccess(const QString& deviceId, const QStringList& barrierIds,
                            const QString& sessionId, const QString& plate, int autoIdleSeconds) {
        QJsonObject root;
        root["type"] = "payment_success";
        root["session_id"] = sessionId;
        root["plate"] = plate;
        root["status"] = "success";
        root["auto_idle_seconds"] = autoIdleSeconds;
        root["timestamp"] = nowIso();
        QJsonArray arr;
        for (const auto& id : barrierIds)
            arr.append(id);
        root["barrier_ids"] = arr;
        publishJson(topicCmd(deviceId), root);
    }

    void sendDisplayMessage(const QString& deviceId, const QString& barrierId,
                            const QString& message, int durationSeconds) {
        QJsonObject root;
        root["type"] = "display_msg";
        root["barrier_id"] = barrierId;
        root["message"] = message;
        root["duration_seconds"] = durationSeconds;
        root["severity"] = "success";
        root["blocking"] = true;
        root["require_ack"] = false;
        root["play_sound"] = false;
        publishJson(topicCmd(deviceId), root);
    }

    BrokerConfig broker_;
    mosquitto* mosq_ = nullptr;
    QMap<QString, DeviceInfo> devices_;
    QMap<QString, SessionInfo> sessions_;
    QString activeDeviceId_;
    QString currentSessionId_;
    QString currentCaseName_;
    QPointer<QTimer> caseTimeout_;
    QString caseTimeoutReason_;
    CaseId currentCase_ = CaseId::Reconnect;
    CaseId nextCase_ = CaseId::Reconnect;
    bool autorun_ = true;
    bool suiteStarted_ = false;
    bool caseActive_ = false;
    bool awaitingReconnectAck_ = false;
    bool currentCaseSawPayment_ = false;
    bool currentCaseSawAbandoned_ = false;
    bool currentCaseSawSupport_ = false;
    int caseCount_ = 0;
    int passCount_ = 0;
    static constexpr int totalCaseCount_ = 6;
};

#include "main.moc"

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);
    app.setApplicationName("parking_test_server");
    app.setApplicationVersion("1.0.0");

    QCommandLineParser parser;
    parser.addHelpOption();
    parser.addVersionOption();
    parser.addOption({{"c", "config"}, "Path to client config JSON", "config", "config.selftest.json"});
    parser.addOption({{"n", "no-autorun"}, "Do not start the self-test suite automatically"});
    parser.process(app);

    const BrokerConfig cfg = loadBrokerConfig(parser.value("config"));
    ParkingTestServer server(cfg, !parser.isSet("no-autorun"));
    if (!server.start())
        return 1;
    return app.exec();
}
