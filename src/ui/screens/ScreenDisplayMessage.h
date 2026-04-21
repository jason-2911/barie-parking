#pragma once

#include <QWidget>

#include "types/MessageTypes.h"

class QLabel;
class QPushButton;
class QTimer;

class ScreenDisplayMessage : public QWidget {
    Q_OBJECT

public:
    explicit ScreenDisplayMessage(QWidget* parent = nullptr);

    void setMessage(const DisplayMessage& dm);
    void stop();

signals:
    void dismissed();

private slots:
    void onTick();

private:
    void applySeverity(const QString& severity);

    QLabel* header_ = nullptr;
    QLabel* title_ = nullptr;
    QLabel* message_ = nullptr;
    QLabel* countdown_ = nullptr;
    QPushButton* ack_button_ = nullptr;
    QTimer* timer_ = nullptr;
    int remaining_ = 0;
};
