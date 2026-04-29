#pragma once
#include <QWidget>
class QLabel;
class QTimer;

/**
 * @brief Full-screen "THANH TOÁN THẤT BẠI" screen.
 *
 * Mirrors ScreenPaymentSuccess but with the red title bar / X badge.
 */
class ScreenPaymentFailed : public QWidget {
    Q_OBJECT
public:
    explicit ScreenPaymentFailed(QWidget* parent = nullptr);
    void startCountdown(int seconds = 5);
    void stopCountdown();

signals:
    void countdownExpired();

private slots:
    void onTick();

private:
    QLabel* lbl_subtext_ = nullptr;
    QTimer* timer_       = nullptr;
    int     remaining_   = 0;
};
