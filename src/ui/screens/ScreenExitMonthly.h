#pragma once
#include <QWidget>
#include "app/Config.h"
#include "types/MessageTypes.h"
class QLabel;
class ScreenExitMonthly : public QWidget {
    Q_OBJECT
public:
    explicit ScreenExitMonthly(const Config& cfg, QWidget* parent = nullptr);
    void setVehicleInfo(const VehicleInfo& vi);
signals:
    void callSupportRequested();
private:
    const Config& cfg_;
    QLabel* lbl_plate_      = nullptr;
    QLabel* lbl_owner_      = nullptr;
    QLabel* lbl_valid_until_= nullptr;
    QLabel* lbl_entry_      = nullptr;
    QLabel* lbl_exit_       = nullptr;
    QLabel* lbl_duration_   = nullptr;
    QLabel* lbl_valid_panel_= nullptr;
};
