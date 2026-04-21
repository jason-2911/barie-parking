#include "ui/screens/ScreenExitMonthly.h"
#include "ui/screens/ScreenHelpers.h"
#include "ui/Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

using namespace ui;

ScreenExitMonthly::ScreenExitMonthly(const Config& cfg, QWidget* parent)
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
        auto* l = new QLabel("VÉ THÁNG HỢP LỆ — MIỄN PHÍ");
        l->setAlignment(Qt::AlignCenter);
        l->setStyleSheet(
            "color:#ffffff;font-size:36px;font-weight:900;"
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

        auto* t = new QLabel("BIỂN SỐ XE");
        t->setStyleSheet(
            "color:#71717a;font-size:14px;font-weight:700;letter-spacing:1.5px;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(t);

        auto* plate_w = makePlateWidget("---.--", &lbl_plate_);
        sv->addWidget(plate_w);

        lv->addWidget(sec);
        lv->addWidget(heavySep());
    }

    // Time tiles (in/out)
    {
        auto* tiles = new QWidget;
        tiles->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        auto* th = new QHBoxLayout(tiles);
        th->setContentsMargins(0, 0, 0, 0);
        th->setSpacing(0);

        auto* t1 = makeInfoTile("GIỜ VÀO", "--:--  --/--", &lbl_entry_,
                                  24, theme::BG_WHITE, theme::TEXT_DARK);
        t1->setStyleSheet(
            QString("background:%1;border-right:4px solid %2;")
            .arg(theme::BG_WHITE, theme::HEAVY_BORDER));
        th->addWidget(t1, 1);

        auto* t2 = makeInfoTile("GIỜ RA", "--:--  --/--", &lbl_exit_,
                                  24, theme::BG_WHITE, theme::TEXT_DARK);
        t2->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        th->addWidget(t2, 1);

        lv->addWidget(tiles);
        lv->addWidget(heavySep());
    }

    // Duration tile (full width, accent)
    {
        auto* dur = makeInfoTile("THỜI GIAN GỬI", "--", &lbl_duration_,
                                   36, "#fff7ed", theme::ORANGE);
        dur->setStyleSheet(
            QString("background:#fff7ed;"));
        lv->addWidget(dur);
        lv->addWidget(heavySep());
    }

    // Owner info section
    {
        auto* sec = new QWidget;
        sec->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        auto* sv = new QVBoxLayout(sec);
        sv->setContentsMargins(theme::SPACE_XL, theme::SPACE_LG,
                                theme::SPACE_XL, theme::SPACE_LG);
        sv->setSpacing(theme::SPACE_SM);

        auto* t = new QLabel("THÔNG TIN CHỦ XE");
        t->setStyleSheet(
            "color:#71717a;font-size:14px;font-weight:700;letter-spacing:1.5px;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(t);

        lbl_owner_ = new QLabel("---");
        lbl_owner_->setStyleSheet(
            "color:#09090b;font-size:28px;font-weight:900;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(lbl_owner_);

        auto* valid_row = new QWidget;
        valid_row->setStyleSheet("background:transparent;");
        auto* vh = new QHBoxLayout(valid_row);
        vh->setContentsMargins(0, 0, 0, 0);
        vh->setSpacing(theme::SPACE_MD);

        auto* vt = new QLabel("HIỆU LỰC ĐẾN:");
        vt->setStyleSheet(
            "color:#71717a;font-size:16px;font-weight:700;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        vh->addWidget(vt);

        lbl_valid_until_ = new QLabel("---");
        lbl_valid_until_->setStyleSheet(
            "color:#f97316;font-size:20px;font-weight:900;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        vh->addWidget(lbl_valid_until_, 1);
        sv->addWidget(valid_row);

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

    // VIP / valid section
    {
        auto* sec = new QWidget;
        sec->setStyleSheet(QString("background:%1;").arg(theme::BG_ZINC200));
        auto* sv = new QVBoxLayout(sec);
        sv->setContentsMargins(theme::SPACE_XL, theme::SPACE_2XL,
                                theme::SPACE_XL, theme::SPACE_2XL);
        sv->setAlignment(Qt::AlignCenter);
        sv->setSpacing(theme::SPACE_LG);

        // VIP badge
        auto* vip_badge = new QLabel("VIP");
        vip_badge->setAlignment(Qt::AlignCenter);
        vip_badge->setFixedSize(96, 40);
        vip_badge->setStyleSheet(
            QString("color:#ffffff;font-size:18px;font-weight:900;letter-spacing:4px;"
                    "background:%1;border:4px solid %2;"
                    "font-family:\"Helvetica Neue\",Arial;")
            .arg(theme::ORANGE, theme::HEAVY_BORDER));
        sv->addWidget(vip_badge, 0, Qt::AlignHCenter);

        // Check icon
        auto* ring = new QWidget;
        ring->setFixedSize(128, 128);
        applyHeavyBox(ring, "#dcfce7");
        auto* chk = new QLabel("✓", ring);
        chk->setAlignment(Qt::AlignCenter);
        chk->resize(128, 128);
        chk->setStyleSheet(
            "color:#16a34a;font-size:64px;font-weight:900;background:transparent;");
        sv->addWidget(ring, 0, Qt::AlignHCenter);

        auto* ok_lbl = new QLabel("VÉ THÁNG HỢP LỆ");
        ok_lbl->setAlignment(Qt::AlignCenter);
        ok_lbl->setStyleSheet(
            "color:#09090b;font-size:24px;font-weight:900;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(ok_lbl);

        auto* ok_sub = new QLabel("Đang mở cần — Miễn phí");
        ok_sub->setAlignment(Qt::AlignCenter);
        ok_sub->setStyleSheet(
            "color:#52525b;font-size:16px;font-weight:700;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(ok_sub);

        // Valid until panel
        auto* vpanel = new QWidget;
        applyHeavyBox(vpanel, theme::BG_WHITE);
        auto* vpv = new QVBoxLayout(vpanel);
        vpv->setContentsMargins(theme::SPACE_LG, theme::SPACE_MD,
                                 theme::SPACE_LG, theme::SPACE_MD);
        vpv->setAlignment(Qt::AlignCenter);
        vpv->setSpacing(4);

        auto* vpt = new QLabel("HIỆU LỰC ĐẾN");
        vpt->setAlignment(Qt::AlignCenter);
        vpt->setStyleSheet(
            "color:#71717a;font-size:13px;font-weight:700;letter-spacing:1.5px;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        vpv->addWidget(vpt);

        lbl_valid_panel_ = new QLabel("---");
        lbl_valid_panel_->setAlignment(Qt::AlignCenter);
        lbl_valid_panel_->setStyleSheet(
            "color:#f97316;font-size:28px;font-weight:900;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        vpv->addWidget(lbl_valid_panel_);

        sv->addWidget(vpanel);
        rv->addWidget(sec, 1);
    }

    body_h->addWidget(right, 45);
}

void ScreenExitMonthly::setVehicleInfo(const VehicleInfo& vi) {
    if (lbl_plate_)        lbl_plate_->setText(vi.plate);
    if (lbl_owner_)        lbl_owner_->setText(vi.owner_name);
    if (lbl_valid_until_)  lbl_valid_until_->setText(vi.valid_until.toString("dd/MM/yyyy"));
    if (lbl_valid_panel_)  lbl_valid_panel_->setText(vi.valid_until.toString("dd/MM/yyyy"));
    if (lbl_entry_)        lbl_entry_->setText(vi.entry_time.toString("HH:mm  dd/MM"));
    if (lbl_exit_)         lbl_exit_->setText(vi.exit_time.toString("HH:mm  dd/MM"));
    if (lbl_duration_) {
        int h = vi.duration_minutes / 60;
        int m = vi.duration_minutes % 60;
        lbl_duration_->setText(h > 0
            ? QString("%1 giờ %2 phút").arg(h).arg(m)
            : QString("%1 phút").arg(m));
    }
}
