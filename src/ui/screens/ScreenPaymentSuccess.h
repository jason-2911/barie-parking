#pragma once
#include <QWidget>
class QLabel;
class QTimer;

/**
 * @brief Full-screen "THANH TOÁN THÀNH CÔNG" confirmation.
 *
 * The screen self-resets after `seconds`, emitting countdownExpired() so the
 * host can return to idle. The countdown also drives the "Tự động reset màn
 * hình trong Ns" sub text.
 */
class ScreenPaymentSuccess : public QWidget {
    Q_OBJECT
public:
    explicit ScreenPaymentSuccess(QWidget* parent = nullptr);
    void setResult(const QString& plate, quint32 amount_paid,
                   const QString& method);
    void startCountdown(int seconds = 5);
    void stopCountdown();

signals:
    void countdownExpired();

private slots:
    void onTick();

private:
    QLabel* lbl_subtext_  = nullptr;
    QTimer* timer_        = nullptr;
    int     remaining_    = 0;
};
