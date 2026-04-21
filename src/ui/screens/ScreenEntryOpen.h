#pragma once
#include <QWidget>
#include "app/Config.h"
class QLabel;
class ScreenEntryOpen : public QWidget {
    Q_OBJECT
public:
    explicit ScreenEntryOpen(const Config& cfg, QWidget* parent = nullptr);
    void refresh();
    void setPlate(const QString& plate);
private:
    const Config& cfg_;
    QLabel* lbl_time_  = nullptr;
    QLabel* lbl_date_  = nullptr;
    QLabel* lbl_plate_ = nullptr;
};
