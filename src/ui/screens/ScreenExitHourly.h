#pragma once
#include <QWidget>
#include "app/Config.h"
#include "types/MessageTypes.h"
class QLabel; class QRWidget;
class ScreenExitHourly : public QWidget {
    Q_OBJECT
public:
    explicit ScreenExitHourly(const Config& cfg, QWidget* parent = nullptr);
    void setVehicleInfo(const VehicleInfo& vi);
    void updatePayment(quint32 received, quint32 remaining);
signals:
    void callSupportRequested();
private:
    const Config& cfg_;
    QLabel* lbl_plate_      = nullptr;
    QLabel* lbl_plate_dark_ = nullptr;
    QLabel* lbl_entry_      = nullptr;
    QLabel* lbl_entry_sub_  = nullptr;
    QLabel* lbl_exit_  = nullptr;
    QLabel* lbl_duration_ = nullptr;
    QLabel* lbl_total_    = nullptr;
    QLabel* lbl_received_ = nullptr;
    QLabel* lbl_remaining_= nullptr;
    QRWidget* qr_widget_  = nullptr;
    VehicleInfo current_vi_;
};
