#pragma once
#include <QWidget>
#include "app/Config.h"
#include "types/MessageTypes.h"

class QLabel;
class QFrame;
class QStackedWidget;
class QTimer;
class QRWidget;
class TitleBar;

/**
 * @brief Hourly-payment screen — left card with vehicle/payment info, right
 *        card with the countdown timer + VietQR.
 *
 * The right card has three visible states driven by the remaining countdown:
 *   - Normal     : message in green, timer in navy
 *   - Warning    : message in green, timer in red (≤ kWarningSeconds left)
 *   - TimedOut   : message in red ("Phiên đăng nhập đã hết hạn"), QR greyed
 *                  out with a refresh badge overlay
 *
 * After the countdown reaches zero the screen sits in TimedOut for
 * kTimedOutHoldSeconds before emitting sessionTimedOut() so the host can
 * route back to idle.
 */
class ScreenExitHourly : public QWidget {
    Q_OBJECT
public:
    explicit ScreenExitHourly(const Config& cfg, QWidget* parent = nullptr);
    void setVehicleInfo(const VehicleInfo& vi);
    void updatePayment(quint32 received, quint32 remaining);

    // Lifecycle helpers used by MainWindow when navigating away.
    void stop();   // halt countdown without firing signals

signals:
    void callSupportRequested();
    void sessionTimedOut();   // fires kTimedOutHoldSeconds after timer hits 0

private slots:
    void onTick();

private:
    enum class QrState { Normal, Warning, TimedOut };
    void setQrState(QrState s);
    void resetCountdown(int seconds);
    void updateTimerLabel();

    const Config& cfg_;

    // Left card
    QLabel* lbl_plate_         = nullptr;
    QLabel* lbl_amount_due_    = nullptr;
    QLabel* lbl_amount_paid_   = nullptr;
    QLabel* lbl_amount_remain_ = nullptr;
    QLabel* lbl_entry_time_    = nullptr;
    QLabel* lbl_entry_date_    = nullptr;
    QLabel* lbl_exit_time_     = nullptr;
    QLabel* lbl_exit_date_     = nullptr;
    QLabel* lbl_duration_      = nullptr;

    // Right card
    QLabel*  lbl_message_     = nullptr;
    QLabel*  lbl_countdown_   = nullptr;
    QRWidget* qr_widget_      = nullptr;
    QWidget* qr_overlay_      = nullptr;   // refresh badge shown in TimedOut
    QFrame*  qr_box_          = nullptr;

    QTimer* tick_timer_       = nullptr;
    int     remaining_secs_   = 0;
    int     timeout_extra_    = 0;
    QrState state_            = QrState::Normal;

    VehicleInfo current_vi_;
};
