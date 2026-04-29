#include "ui/widgets/TopBar.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QDateTime>

TopBar::TopBar(const Config& cfg, QWidget* parent)
    : QWidget(parent), cfg_(cfg)
{
    setFixedHeight(68);
    setObjectName("topbar");

    // Plain QWidget does not paint stylesheet backgrounds unless
    // WA_StyledBackground is set; without it the navy fill is dropped on
    // the floor and the parent's grey shows through.
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(
        QString("QWidget#topbar {background:%1;}").arg(theme::V2_HEADER_NAVY));

    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(28, 0, 28, 0);
    h->setSpacing(0);

    lbl_dir_ = new QLabel(cfg_.device.description.toUpper());
    lbl_dir_->setStyleSheet(
        "color:#FFFFFF;font-size:28px;font-weight:700;background:transparent;"
        "font-family:\"Inter\",\"Helvetica Neue\",Arial;");
    h->addWidget(lbl_dir_, 0, Qt::AlignVCenter | Qt::AlignLeft);

    h->addStretch();

    lbl_time_ = new QLabel;
    lbl_time_->setStyleSheet(
        "color:#FFFFFF;font-size:28px;font-weight:700;background:transparent;"
        "font-family:\"Inter\",\"Helvetica Neue\",Arial;");
    h->addWidget(lbl_time_, 0, Qt::AlignVCenter | Qt::AlignRight);

    lbl_date_ = nullptr;

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &TopBar::tick);
    timer->start(1000);
    tick();
}

void TopBar::tick() {
    lbl_time_->setText(QDateTime::currentDateTime().toString("HH:mm:ss"));
}

void TopBar::setDirection(BarrierDirection) { /* not shown in v2 design */ }
