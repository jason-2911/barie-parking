#pragma once

#include <QMainWindow>
#include <QStackedWidget>
#include <QLabel>
#include <QTimer>
#include "app/Config.h"
#include "types/MessageTypes.h"

class QVBoxLayout;
class TopBar;
class Footer;
class ScreenEntryIdle;
class ScreenEntryOpen;
class ScreenExitHourly;
class ScreenExitMonthly;
class ScreenPaymentSuccess;
class ScreenPaymentFailed;
class ScreenDisplayMessage;

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit MainWindow(const Config& cfg, QWidget* parent = nullptr);

    // Screen transitions
    void showIdle();
    void showBarrierOpening(const QString& barrier_id, const QString& plate = {});
    void showExitHourly(const VehicleInfo& vi);
    void showExitMonthly(const VehicleInfo& vi);
    // countdown_seconds: 0 = use device default (15 s)
    void showPaymentSuccess(const QString& plate, quint32 amount,
                             const QString& method = "QR Pay",
                             int countdown_seconds = 5);
    void showPaymentFailed(int countdown_seconds = 5);
    void showDisplayMessage(const DisplayMessage& dm);

    // Status updates (no-op — status bar removed)
    void setMqttStatus(bool connected)                              { Q_UNUSED(connected) }
    void setBarrierStatus(const QString& id, const QString& pos)   { Q_UNUSED(id) Q_UNUSED(pos) }
    void setStatusMessage(const QString& msg)                      { Q_UNUSED(msg) }
    void updatePaymentReceived(quint32 received, quint32 remaining);
    void showNotification(const QString& msg, int duration_ms);

signals:
    void callSupportRequested();

private:
    void buildLayout();
    void setupNotification();
    void restoreAfterDisplayMessage();

    const Config& cfg_;

    QWidget*         central_  = nullptr;
    QVBoxLayout*     vbox_     = nullptr;
    TopBar*          topbar_   = nullptr;
    Footer*          footer_   = nullptr;
    QStackedWidget*  stack_    = nullptr;

    ScreenEntryIdle*       screen_idle_     = nullptr;
    ScreenEntryOpen*       screen_entry_open_= nullptr;
    ScreenExitHourly*      screen_exit_h_   = nullptr;
    ScreenExitMonthly*     screen_exit_m_   = nullptr;
    ScreenPaymentSuccess*  screen_success_  = nullptr;
    ScreenPaymentFailed*   screen_failed_   = nullptr;
    ScreenDisplayMessage*  screen_display_  = nullptr;

    QLabel*  notif_label_ = nullptr;
    QTimer*  notif_timer_ = nullptr;

    enum class Screen { Idle, EntryOpen, ExitHourly, ExitMonthly, Success, Failed, DisplayMessage };
    Screen current_screen_ = Screen::Idle;
    Screen previous_screen_before_display_ = Screen::Idle;
};
