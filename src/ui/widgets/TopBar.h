#pragma once
#include <QWidget>
#include "app/Config.h"
#include "types/MessageTypes.h"
class QLabel;
class TopBar : public QWidget {
    Q_OBJECT
public:
    explicit TopBar(const Config& cfg, QWidget* parent = nullptr);
    void setDirection(BarrierDirection dir);
private slots:
    void tick();
private:
    const Config& cfg_;
    QLabel* lbl_time_ = nullptr;
    QLabel* lbl_date_ = nullptr;
    QLabel* lbl_dir_  = nullptr;
};
