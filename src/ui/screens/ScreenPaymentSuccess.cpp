#include "ui/screens/ScreenPaymentSuccess.h"
#include "ui/screens/ScreenHelpers.h"
#include "ui/Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>

using namespace ui;

ScreenPaymentSuccess::ScreenPaymentSuccess(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet(QString("background:%1;").arg(theme::BG_BODY));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Green banner ──────────────────────────────────────────────────
    {
        auto* b = new QWidget;
        b->setFixedHeight(72);
        b->setStyleSheet(QString("background:%1;").arg(theme::GREEN_HEADER));
        auto* bh = new QHBoxLayout(b);
        bh->setContentsMargins(0, 0, 0, 0);
        auto* l = new QLabel("THANH TOÁN THÀNH CÔNG");
        l->setAlignment(Qt::AlignCenter);
        l->setStyleSheet(
            "color:#ffffff;font-size:38px;font-weight:900;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        bh->addWidget(l);
        root->addWidget(b);
        root->addWidget(heavySep());
    }

    // ── Two-column body ───────────────────────────────────────────────
    auto* body = new QWidget;
    body->setStyleSheet(QString("background:%1;").arg(theme::BG_BODY));
    auto* body_h = new QHBoxLayout(body);
    body_h->setContentsMargins(0, 0, 0, 0);
    body_h->setSpacing(0);
    root->addWidget(body, 1);

    // ════════════════════════════════════════════════════════════════
    // LEFT column (55%)
    // ════════════════════════════════════════════════════════════════
    auto* left = new QWidget;
    left->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(0);

    // Check icon + title section
    {
        auto* sec = new QWidget;
        sec->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        auto* sv = new QVBoxLayout(sec);
        sv->setContentsMargins(theme::SPACE_XL, theme::SPACE_2XL,
                                theme::SPACE_XL, theme::SPACE_XL);
        sv->setAlignment(Qt::AlignCenter);
        sv->setSpacing(theme::SPACE_MD);

        auto* check_ring = new QWidget;
        check_ring->setFixedSize(96, 96);
        applyHeavyBox(check_ring, "#dcfce7");
        auto* chk_lbl = new QLabel("✓", check_ring);
        chk_lbl->setAlignment(Qt::AlignCenter);
        chk_lbl->resize(96, 96);
        chk_lbl->setStyleSheet(
            "color:#16a34a;font-size:52px;font-weight:900;background:transparent;");
        sv->addWidget(check_ring, 0, Qt::AlignHCenter);

        auto* title = new QLabel("GIAO DỊCH THÀNH CÔNG");
        title->setAlignment(Qt::AlignCenter);
        title->setStyleSheet(
            "color:#09090b;font-size:32px;font-weight:900;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(title);

        auto* sub = new QLabel("Cảm ơn quý khách. Vui lòng di chuyển ra cổng.");
        sub->setAlignment(Qt::AlignCenter);
        sub->setWordWrap(true);
        sub->setStyleSheet(
            "color:#52525b;font-size:18px;background:transparent;"
            "font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(sub);

        lv->addWidget(sec);
        lv->addWidget(heavySep());
    }

    // Three info tiles
    {
        auto* tiles_outer = new QWidget;
        tiles_outer->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        auto* th = new QHBoxLayout(tiles_outer);
        th->setContentsMargins(0, 0, 0, 0);
        th->setSpacing(0);

        auto* t1 = makeInfoTile("BIỂN SỐ XE", "---", &lbl_plate_,
                                  28, theme::BG_WHITE, theme::TEXT_DARK);
        th->addWidget(t1, 1);
        th->addWidget(heavyVSep());

        auto* t2 = makeInfoTile("SỐ TIỀN THANH TOÁN", "0 \u20ab", &lbl_amount_,
                                  28, "#dcfce7", "#16a34a");
        t2->setStyleSheet("background:#dcfce7;");
        th->addWidget(t2, 1);
        th->addWidget(heavyVSep());

        auto* t3 = makeInfoTile("PHƯƠNG THỨC", "---", &lbl_method_,
                                  22, theme::BG_WHITE, theme::TEXT_DARK);
        th->addWidget(t3, 1);

        lv->addWidget(tiles_outer);
        lv->addWidget(heavySep());
    }

    // Countdown banner (emerald)
    {
        auto* sec = new QWidget;
        sec->setStyleSheet("background:#052e16;");
        auto* sv = new QVBoxLayout(sec);
        sv->setContentsMargins(theme::SPACE_XL, theme::SPACE_2XL,
                                theme::SPACE_XL, theme::SPACE_2XL);
        sv->setAlignment(Qt::AlignCenter);
        sv->setSpacing(theme::SPACE_SM);

        auto* cd_title = new QLabel("MỜI XE RA CỔNG TRONG");
        cd_title->setAlignment(Qt::AlignCenter);
        cd_title->setStyleSheet(
            "color:rgba(255,255,255,0.7);font-size:18px;font-weight:700;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(cd_title);

        lbl_seconds_ = new QLabel("15");
        lbl_seconds_->setAlignment(Qt::AlignCenter);
        lbl_seconds_->setStyleSheet(
            "color:#4ade80;font-size:96px;font-weight:900;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(lbl_seconds_);

        auto* cd_unit = new QLabel("GIÂY");
        cd_unit->setAlignment(Qt::AlignCenter);
        cd_unit->setStyleSheet(
            "color:rgba(255,255,255,0.6);font-size:18px;font-weight:700;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(cd_unit);

        lv->addWidget(sec, 1);
    }

    body_h->addWidget(left, 55);
    body_h->addWidget(heavyVSep());

    // ════════════════════════════════════════════════════════════════
    // RIGHT column (45%)
    // ════════════════════════════════════════════════════════════════
    auto* right = new QWidget;
    right->setStyleSheet(QString("background:%1;").arg(theme::BG_ZINC200));
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(0);

    // Car icon + ready section
    {
        auto* sec = new QWidget;
        sec->setStyleSheet(QString("background:%1;").arg(theme::BG_ZINC200));
        auto* sv = new QVBoxLayout(sec);
        sv->setContentsMargins(theme::SPACE_XL, theme::SPACE_2XL,
                                theme::SPACE_XL, theme::SPACE_2XL);
        sv->setAlignment(Qt::AlignCenter);
        sv->setSpacing(theme::SPACE_XL);

        auto* car_ring = new QWidget;
        car_ring->setFixedSize(160, 160);
        applyHeavyBox(car_ring, "#dcfce7");
        auto* car_chk = new QLabel("✓", car_ring);
        car_chk->setAlignment(Qt::AlignCenter);
        car_chk->resize(160, 160);
        car_chk->setStyleSheet(
            "color:#16a34a;font-size:96px;font-weight:900;background:transparent;");
        sv->addWidget(car_ring, 0, Qt::AlignHCenter);

        auto* ok_lbl = new QLabel("CỔNG ĐÃ MỞ");
        ok_lbl->setAlignment(Qt::AlignCenter);
        ok_lbl->setStyleSheet(
            "color:#09090b;font-size:32px;font-weight:900;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(ok_lbl);

        auto* ok_sub = new QLabel("Di chuyển chậm, cẩn thận qua cần");
        ok_sub->setAlignment(Qt::AlignCenter);
        ok_sub->setStyleSheet(
            "color:#52525b;font-size:18px;background:transparent;"
            "font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(ok_sub);

        rv->addWidget(sec, 1);
        rv->addWidget(heavySep());
    }

    // Action buttons
    {
        auto* btns = new QWidget;
        btns->setStyleSheet("background:transparent;");
        auto* bh2 = new QHBoxLayout(btns);
        bh2->setContentsMargins(0, 0, 0, 0);
        bh2->setSpacing(0);

        auto* btn_print = makeActionBtn("IN HÓA ĐƠN", theme::BG_ZINC900, "#ffffff");
        bh2->addWidget(btn_print, 1);

        bh2->addWidget(heavyVSep());

        auto* btn_done = makeActionBtn("RA CỔNG NGAY", theme::GREEN_HEADER, "#ffffff");
        btn_done->setObjectName("btnSuccess");
        connect(btn_done, &QPushButton::clicked,
                this, &ScreenPaymentSuccess::countdownExpired);
        bh2->addWidget(btn_done, 1);

        rv->addWidget(btns);
    }

    body_h->addWidget(right, 45);

    // Timer setup
    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &ScreenPaymentSuccess::onTick);
}

void ScreenPaymentSuccess::setResult(const QString& plate,
                                      quint32         amount_paid,
                                      const QString& method)
{
    if (lbl_plate_)  lbl_plate_->setText(plate);
    if (lbl_amount_) lbl_amount_->setText(
        QString("%L1 \u20ab").arg(amount_paid));
    if (lbl_method_) lbl_method_->setText(method);
}

void ScreenPaymentSuccess::startCountdown(int seconds) {
    remaining_ = seconds;
    if (lbl_seconds_) lbl_seconds_->setText(QString::number(remaining_));
    timer_->start();
}

void ScreenPaymentSuccess::stopCountdown() {
    timer_->stop();
}

void ScreenPaymentSuccess::onTick() {
    --remaining_;
    if (lbl_seconds_) lbl_seconds_->setText(QString::number(remaining_));
    if (remaining_ <= 0) {
        timer_->stop();
        emit countdownExpired();
    }
}
