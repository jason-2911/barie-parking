#pragma once
#include <QWidget>
class QLabel;
class QTimer;

class ScreenPaymentSuccess : public QWidget {
    Q_OBJECT
public:
    explicit ScreenPaymentSuccess(QWidget* parent = nullptr);
    void setResult(const QString& plate, quint32 amount_paid,
                   const QString& method);
    void startCountdown(int seconds = 15);
    void stopCountdown();   // call when navigating away to cancel the timer

signals:
    void countdownExpired();    // go back to idle

private slots:
    void onTick();

private:
    QLabel* lbl_plate_   = nullptr;
    QLabel* lbl_amount_  = nullptr;
    QLabel* lbl_method_  = nullptr;
    QLabel* lbl_seconds_ = nullptr;
    QTimer* timer_       = nullptr;
    int     remaining_   = 0;
};
