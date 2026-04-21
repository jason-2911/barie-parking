#pragma once
#include <QWidget>
#include "app/Config.h"
class QLabel;
class BottomBar : public QWidget {
    Q_OBJECT
public:
    explicit BottomBar(const Config& cfg, QWidget* parent = nullptr);
    void setMqttStatus(bool connected);
    void setBarrierStatus(const QString& id, const QString& position);
    void setMessage(const QString& msg);
private:
    QLabel* dot_mqtt_    = nullptr;
    QLabel* lbl_mqtt_    = nullptr;
    QLabel* dot_barrier_ = nullptr;
    QLabel* lbl_barrier_ = nullptr;
    QLabel* lbl_msg_     = nullptr;
    QLabel* lbl_device_  = nullptr;
};
