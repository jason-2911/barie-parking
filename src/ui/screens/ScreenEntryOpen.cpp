#include "ui/screens/ScreenEntryOpen.h"
#include "ui/screens/ScreenHelpers.h"
#include "ui/Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QDateTime>

using namespace ui;

ScreenEntryOpen::ScreenEntryOpen(const Config& cfg, QWidget* parent)
    : QWidget(parent), cfg_(cfg)
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
        auto* l = new QLabel("CẦN ĐANG MỞ — XE VÀO");
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

    // ── Left column (55%) ─────────────────────────────────────────────
    auto* left = new QWidget;
    left->setStyleSheet(QString("background:%1;").arg(theme::BG_LEFT));
    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(0, 0, 0, 0);
    lv->setSpacing(0);

    // Plate section
    {
        auto* sec = new QWidget;
        sec->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        auto* sv = new QVBoxLayout(sec);
        sv->setContentsMargins(theme::SPACE_XL, theme::SPACE_XL,
                                theme::SPACE_XL, theme::SPACE_XL);
        sv->setSpacing(theme::SPACE_MD);

        auto* t = new QLabel("BIỂN SỐ XE VÀO");
        t->setStyleSheet(
            "color:#71717a;font-size:14px;font-weight:700;letter-spacing:1.5px;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(t);

        auto* plate_w = makePlateWidget("---.--", &lbl_plate_);
        sv->addWidget(plate_w);

        lv->addWidget(sec);
        lv->addWidget(heavySep());
    }

    // Time/date tiles — split by a heavyVSep so neither tile carries a
    // border-right (unscoped QSS borders cascade into child labels and
    // render as duplicate vertical bars).
    {
        auto* tiles = new QWidget;
        tiles->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        auto* th = new QHBoxLayout(tiles);
        th->setContentsMargins(0, 0, 0, 0);
        th->setSpacing(0);

        auto* t1 = makeInfoTile("GIỜ VÀO", "--:--:--", &lbl_time_,
                                  40, theme::BG_WHITE, theme::TEXT_DARK);
        th->addWidget(t1, 1);
        th->addWidget(heavyVSep());

        auto* t2 = makeInfoTile("NGÀY", "--/--/----", &lbl_date_,
                                  40, theme::BG_WHITE, theme::TEXT_DARK);
        th->addWidget(t2, 1);

        lv->addWidget(tiles);
        lv->addWidget(heavySep());
    }

    // Dark status section
    {
        auto* sec = new QWidget;
        sec->setStyleSheet(QString("background:%1;").arg(theme::BG_ZINC900));
        auto* sv = new QVBoxLayout(sec);
        sv->setContentsMargins(theme::SPACE_XL, theme::SPACE_2XL,
                                theme::SPACE_XL, theme::SPACE_2XL);
        sv->setAlignment(Qt::AlignCenter);
        sv->setSpacing(theme::SPACE_MD);

        auto* s_label = new QLabel("TRẠNG THÁI CỔNG");
        s_label->setAlignment(Qt::AlignCenter);
        s_label->setStyleSheet(
            "color:rgba(255,255,255,0.6);font-size:18px;font-weight:700;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(s_label);

        auto* s_val = new QLabel("ĐANG MỞ");
        s_val->setAlignment(Qt::AlignCenter);
        s_val->setStyleSheet(
            "color:#4ade80;font-size:56px;font-weight:900;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(s_val);

        lv->addWidget(sec, 1);
    }

    body_h->addWidget(left, 55);
    body_h->addWidget(heavyVSep());

    // ── Right column (45%) ────────────────────────────────────────────
    auto* right = new QWidget;
    right->setStyleSheet(QString("background:%1;").arg(theme::BG_ZINC200));
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(0);

    {
        auto* sec = new QWidget;
        sec->setStyleSheet(QString("background:%1;").arg(theme::BG_ZINC200));
        auto* sv = new QVBoxLayout(sec);
        sv->setContentsMargins(theme::SPACE_XL, theme::SPACE_2XL,
                                theme::SPACE_XL, theme::SPACE_2XL);
        sv->setAlignment(Qt::AlignCenter);
        sv->setSpacing(theme::SPACE_XL);

        auto* ok_ring = new QWidget;
        ok_ring->setFixedSize(160, 160);
        applyHeavyBox(ok_ring, "#dcfce7");
        auto* chk = new QLabel("✓", ok_ring);
        chk->setAlignment(Qt::AlignCenter);
        chk->resize(160, 160);
        chk->setStyleSheet(
            "color:#16a34a;font-size:80px;font-weight:900;background:transparent;");
        sv->addWidget(ok_ring, 0, Qt::AlignHCenter);

        auto* ok_lbl = new QLabel("CẦN ĐÃ MỞ");
        ok_lbl->setAlignment(Qt::AlignCenter);
        ok_lbl->setStyleSheet(
            "color:#09090b;font-size:28px;font-weight:900;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(ok_lbl);

        auto* ok_sub = new QLabel("Phương tiện vui lòng đi chậm qua cần");
        ok_sub->setAlignment(Qt::AlignCenter);
        ok_sub->setWordWrap(true);
        ok_sub->setStyleSheet(
            "color:#52525b;font-size:18px;background:transparent;"
            "font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(ok_sub);

        rv->addWidget(sec, 1);
    }

    body_h->addWidget(right, 45);
}

void ScreenEntryOpen::refresh() {
    QDateTime now = QDateTime::currentDateTime();
    if (lbl_time_) lbl_time_->setText(now.toString("HH:mm:ss"));
    if (lbl_date_) lbl_date_->setText(now.toString("dd/MM/yyyy"));
}

void ScreenEntryOpen::setPlate(const QString& plate) {
    if (!lbl_plate_) return;
    lbl_plate_->setText(plate.isEmpty() ? QStringLiteral("---.--") : plate);
}
