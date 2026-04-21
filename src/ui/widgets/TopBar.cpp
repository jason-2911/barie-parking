#include "ui/widgets/TopBar.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QDateTime>

TopBar::TopBar(const Config& cfg, QWidget* parent)
    : QWidget(parent), cfg_(cfg)
{
    setFixedHeight(88);
    setObjectName("topbar");

    // Heavy bottom border scoped to TopBar only (so children don't inherit)
    setStyleSheet(
        QString("QWidget#topbar {background:%1;"
                "border-bottom:4px solid %2;}")
        .arg(theme::GREEN_HEADER, theme::HEAVY_BORDER));

    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    // ── Left panel: location info ─────────────────────────────────────
    auto* left = new QWidget;
    left->setStyleSheet(
        QString("background:%1;").arg(theme::GREEN_HEADER));
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(theme::SPACE_XL, theme::SPACE_MD,
                            theme::SPACE_XL, theme::SPACE_MD);
    lv->setSpacing(2);

    auto* lane_lbl = new QLabel("LÀN THANH TOÁN TỰ ĐỘNG");
    lane_lbl->setStyleSheet(
        "color:rgba(255,255,255,0.92);font-size:14px;font-weight:600;"
        "letter-spacing:2px;background:transparent;"
        "font-family:\"Helvetica Neue\",Arial;");
    lv->addWidget(lane_lbl);

    auto* loc_lbl = new QLabel(cfg_.device.description.toUpper());
    loc_lbl->setStyleSheet(
        "color:#ffffff;font-size:32px;font-weight:900;"
        "letter-spacing:2px;background:transparent;"
        "font-family:\"Helvetica Neue\",Arial;");
    lv->addWidget(loc_lbl);

    h->addWidget(left, 1);

    // ── Thick vertical divider ────────────────────────────────────────
    auto* divider = new QFrame;
    divider->setFixedWidth(4);
    divider->setStyleSheet(
        QString("background:%1;border:none;").arg(theme::HEAVY_BORDER));
    h->addWidget(divider);

    // ── Right panel: clock ────────────────────────────────────────────
    auto* right = new QWidget;
    right->setFixedWidth(240);
    right->setStyleSheet(
        QString("background:%1;").arg(theme::GREEN_CLOCK));
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(theme::SPACE_XL, theme::SPACE_MD,
                            theme::SPACE_XL, theme::SPACE_MD);
    rv->setSpacing(2);
    rv->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    auto* time_title = new QLabel("THỜI GIAN");
    time_title->setAlignment(Qt::AlignRight);
    time_title->setStyleSheet(
        "color:rgba(255,255,255,0.85);font-size:14px;font-weight:600;"
        "letter-spacing:2px;background:transparent;"
        "font-family:\"Helvetica Neue\",Arial;");
    rv->addWidget(time_title);

    lbl_time_ = new QLabel;
    lbl_time_->setAlignment(Qt::AlignRight);
    lbl_time_->setStyleSheet(
        "color:#ffffff;font-size:32px;font-weight:900;"
        "letter-spacing:2px;background:transparent;"
        "font-family:\"Helvetica Neue\",Arial;");
    rv->addWidget(lbl_time_);

    h->addWidget(right);

    // hidden compat members
    lbl_dir_  = nullptr;
    lbl_date_ = nullptr;

    auto* timer = new QTimer(this);
    connect(timer, &QTimer::timeout, this, &TopBar::tick);
    timer->start(1000);
    tick();
}

void TopBar::tick() {
    lbl_time_->setText(QDateTime::currentDateTime().toString("HH:mm:ss"));
}

void TopBar::setDirection(BarrierDirection) { /* not shown in this design */ }
