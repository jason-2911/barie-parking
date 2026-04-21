#include "ui/screens/ScreenExitHourly.h"
#include "ui/screens/ScreenHelpers.h"
#include "ui/widgets/QRWidget.h"
#include "ui/Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QPushButton>

using namespace ui;

static const char* kBanks[][2] = {
    {"VCB",  "Vietcombank"}, {"BIDV", "BIDV"},
    {"VTB",  "VietinBank"},  {"AGB",  "Agribank"},
    {"TCB",  "Techcombank"}, {"MB",   "MB Bank"},
    {"ACB",  "ACB"},         {"TPB",  "TPBank"},
    {"VPB",  "VPBank"},      {"SCB",  "Sacombank"},
    {"SHB",  "SHB"},         {"HDB",  "HDBank"}
};

ScreenExitHourly::ScreenExitHourly(const Config& cfg, QWidget* parent)
    : QWidget(parent), cfg_(cfg)
{
    setStyleSheet(QString("background:%1;").arg(theme::BG_BODY));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Orange banner ─────────────────────────────────────────────────
    {
        auto* b = new QWidget;
        b->setFixedHeight(88);
        b->setStyleSheet(QString("background:%1;").arg(theme::ORANGE));
        auto* bh = new QHBoxLayout(b);
        bh->setContentsMargins(0, 0, 0, 0);
        auto* l = new QLabel("THANH TOÁN PHÍ GỬI XE");
        l->setAlignment(Qt::AlignCenter);
        l->setStyleSheet(
            "color:#ffffff;font-size:46px;font-weight:900;letter-spacing:3px;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        bh->addWidget(l);
        root->addWidget(b);
        root->addWidget(heavySep());
    }

    // ── Two-column body ───────────────────────────────────────────────
    // Grid-based so the top row (info ↔ QR) stays height-synced across
    // columns. A plain per-column VBox drifts whenever either side has a
    // bigger min-height than its stretch share would predict — here the
    // GIỜ VÀO tile pushes the info section past its 8/12 share.
    auto* body = new QWidget;
    body->setStyleSheet(QString("background:%1;").arg(theme::BG_BODY));
    auto* body_g = new QGridLayout(body);
    body_g->setContentsMargins(0, 0, 0, 0);
    body_g->setHorizontalSpacing(0);
    body_g->setVerticalSpacing(0);
    body_g->setColumnStretch(0, 1);
    body_g->setColumnStretch(2, 1);
    body_g->setRowStretch(0, 8);
    body_g->setRowStretch(2, 4);
    root->addWidget(body, 1);

    QWidget* info_section    = nullptr;
    QWidget* payment_section = nullptr;
    QWidget* qr_section      = nullptr;
    QWidget* bank_section    = nullptr;
    QWidget* plate_section   = nullptr;

    // ════════════════════════════════════════════════════════════════
    // LEFT column — vehicle info section
    // ════════════════════════════════════════════════════════════════

    // Vehicle info section — ticket QR placeholder removed; the full width
    // is now available for plate, status and info tiles.
    {
        auto* sec = new QWidget;
        sec->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        auto* sh = new QHBoxLayout(sec);
        sh->setContentsMargins(0, 0, 0, 0);
        sh->setSpacing(0);

        auto* info_cell = new QWidget;
        info_cell->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        auto* iv = new QVBoxLayout(info_cell);
        iv->setContentsMargins(theme::SPACE_LG, theme::SPACE_LG,
                                theme::SPACE_LG, theme::SPACE_SM);
        iv->setSpacing(0);

        // Plate row
        auto* plate_row = new QWidget;
        plate_row->setStyleSheet("background:transparent;");
        auto* pr_h = new QHBoxLayout(plate_row);
        pr_h->setContentsMargins(0, 0, 0, 0);
        pr_h->setSpacing(theme::SPACE_MD);

        auto* plate_col = new QVBoxLayout;
        plate_col->setSpacing(2);
        auto* plate_title = new QLabel("BIỂN SỐ XE");
        plate_title->setStyleSheet(
            "color:#71717a;font-size:17px;font-weight:700;letter-spacing:1.5px;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        plate_col->addWidget(plate_title);
        lbl_plate_ = new QLabel("---.--");
        lbl_plate_->setStyleSheet(
            "color:#09090b;font-size:58px;font-weight:900;letter-spacing:2px;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        plate_col->addWidget(lbl_plate_);
        pr_h->addLayout(plate_col, 1);

        // Status badge — size flexes to content
        auto* status_box = new QWidget;
        status_box->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);
        applyHeavyBox(status_box, theme::DANGER_LIGHT);
        auto* sv = new QVBoxLayout(status_box);
        sv->setContentsMargins(theme::SPACE_MD, theme::SPACE_SM,
                                theme::SPACE_MD, theme::SPACE_SM);
        sv->setAlignment(Qt::AlignCenter);
        auto* s_val = new QLabel("CHƯA THANH TOÁN");
        s_val->setAlignment(Qt::AlignCenter);
        s_val->setWordWrap(true);
        s_val->setStyleSheet(
            "color:#b91c1c;font-size:19px;font-weight:900;background:transparent;"
            "font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(s_val);
        pr_h->addWidget(status_box, 0, Qt::AlignVCenter);
        iv->addWidget(plate_row);
        iv->addSpacing(theme::SPACE_MD);

        // Three info tiles row — use heavyVSep between for clean dividers
        auto* tiles_outer = new QWidget;
        tiles_outer->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        auto* tiles_h = new QHBoxLayout(tiles_outer);
        tiles_h->setContentsMargins(0, 0, 0, 0);
        tiles_h->setSpacing(0);

        lbl_exit_ = nullptr; lbl_duration_ = nullptr; lbl_received_ = nullptr;

        // entry tile
        auto* t1 = makeInfoTile("GIỜ VÀO", "--/--/----", &lbl_entry_,
                                  24, theme::BG_WHITE, theme::TEXT_DARK);
        lbl_entry_sub_ = new QLabel("--:--:--");
        lbl_entry_sub_->setStyleSheet(
            "color:#52525b;font-size:20px;font-weight:700;background:transparent;"
            "font-family:\"Helvetica Neue\",Arial;");
        if (auto* v = qobject_cast<QVBoxLayout*>(t1->layout()))
            v->insertWidget(3, lbl_entry_sub_);
        tiles_h->addWidget(t1, 1);
        tiles_h->addWidget(heavyVSep());

        auto* t2 = makeInfoTile("PHÍ GỬI XE", "0 \u20ab", &lbl_total_,
                                  36, theme::BG_WHITE, theme::TEXT_DARK);
        tiles_h->addWidget(t2, 1);
        tiles_h->addWidget(heavyVSep());

        auto* t3 = makeInfoTile("CÒN THIẾU", "0 \u20ab", &lbl_remaining_,
                                  36, theme::DANGER_LIGHT, theme::DANGER);
        tiles_h->addWidget(t3, 1);

        // Outer heavy border (scoped so children don't inherit)
        auto* tiles_wrapper = new QWidget;
        tiles_wrapper->setObjectName("tilesWrap");
        tiles_wrapper->setStyleSheet(
            QString("QWidget#tilesWrap {background:%1;border:4px solid %2;}")
            .arg(theme::BG_WHITE, theme::HEAVY_BORDER));
        auto* tw_v = new QVBoxLayout(tiles_wrapper);
        tw_v->setContentsMargins(0, 0, 0, 0);
        tw_v->setSpacing(0);
        tw_v->addWidget(tiles_outer);

        iv->addWidget(tiles_wrapper);
        sh->addWidget(info_cell, 1);
        info_section = sec;
    }

    // Payment method section
    {
        auto* sec = new QWidget;
        sec->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        auto* sv = new QVBoxLayout(sec);
        sv->setContentsMargins(0, 0, 0, 0);
        sv->setSpacing(0);

        // Header
        auto* hdr = new QWidget;
        hdr->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        auto* hv = new QVBoxLayout(hdr);
        hv->setContentsMargins(theme::SPACE_LG, theme::SPACE_MD,
                                theme::SPACE_LG, theme::SPACE_MD);
        hv->setSpacing(4);
        auto* h_title = new QLabel("PHƯƠNG THỨC THANH TOÁN");
        h_title->setStyleSheet(
            "color:#71717a;font-size:17px;font-weight:700;letter-spacing:1.5px;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        hv->addWidget(h_title);
        auto* h_sub = new QLabel("CHỌN 1 TRONG 2 HÌNH THỨC");
        h_sub->setStyleSheet(
            "color:#09090b;font-size:34px;font-weight:900;background:transparent;"
            "font-family:\"Helvetica Neue\",Arial;");
        hv->addWidget(h_sub);
        sv->addWidget(hdr);
        sv->addWidget(heavySep());

        // Mode buttons
        auto* modes = new QWidget;
        modes->setStyleSheet("background:transparent;");
        auto* mh = new QHBoxLayout(modes);
        mh->setContentsMargins(0, 0, 0, 0);
        mh->setSpacing(0);

        auto makeMode = [](const QString& symbol, const QString& title,
                            const QString& desc, bool active,
                            bool left_border) -> QWidget* {
            // Scope the QSS to this widget via an objectName so the
            // left border cannot cascade into child QLabels (which would
            // otherwise each paint their own vertical bar).
            static int mode_counter = 0;
            auto* card = new QWidget;
            card->setObjectName(QString("modeCard_%1").arg(++mode_counter));
            QString bg = active ? theme::ORANGE_100 : "#ffffff";
            QString cs = QString("QWidget#%1 {background:%2;%3}")
                             .arg(card->objectName(), bg,
                                   left_border
                                       ? QString("border-left:4px solid %1;")
                                             .arg(theme::HEAVY_BORDER)
                                       : QString{});
            card->setStyleSheet(cs);

            auto* cv = new QVBoxLayout(card);
            cv->setContentsMargins(theme::SPACE_XL, theme::SPACE_XL,
                                    theme::SPACE_XL, theme::SPACE_XL);
            cv->setSpacing(theme::SPACE_MD);

            auto* icon = new QWidget;
            icon->setMinimumSize(100, 100);
            applyHeavyBox(icon, active ? theme::ORANGE : theme::BG_ZINC100);
            auto* icon_v = new QVBoxLayout(icon);
            icon_v->setContentsMargins(0, 0, 0, 0);
            icon_v->setAlignment(Qt::AlignCenter);
            auto* icon_lbl = new QLabel(symbol);
            icon_lbl->setAlignment(Qt::AlignCenter);
            icon_lbl->setStyleSheet(
                QString("color:%1;font-size:60px;font-weight:900;background:transparent;"
                        "font-family:\"Helvetica Neue\",Arial;")
                .arg(active ? "#ffffff" : theme::HEAVY_BORDER));
            icon_v->addWidget(icon_lbl);
            cv->addWidget(icon);

            auto* t = new QLabel(title);
            t->setStyleSheet(
                "color:#09090b;font-size:34px;font-weight:900;background:transparent;"
                "font-family:\"Helvetica Neue\",Arial;");
            cv->addWidget(t);

            auto* d = new QLabel(desc);
            d->setWordWrap(true);
            d->setStyleSheet(
                "color:#52525b;font-size:19px;font-weight:600;background:transparent;"
                "font-family:\"Helvetica Neue\",Arial;");
            cv->addWidget(d);
            cv->addStretch();
            return card;
        };

        mh->addWidget(makeMode("\u20ab", "TIỀN MẶT",
            "Nạp tiền trực tiếp vào máy.", false, false), 1);
        mh->addWidget(makeMode("QR", "QUÉT QR",
            "Ngân hàng hoặc ví điện tử.", true, true), 1);
        sv->addWidget(modes);
        payment_section = sec;
    }

    // ════════════════════════════════════════════════════════════════
    // RIGHT column — QR / bank / plate sections
    // ════════════════════════════════════════════════════════════════

    // QR section — fill the entire right-top cell.
    {
        auto* sec = new QWidget;
        sec->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        auto* sv = new QVBoxLayout(sec);
        sv->setContentsMargins(theme::SPACE_LG, theme::SPACE_MD,
                                theme::SPACE_LG, theme::SPACE_SM);
        sv->setSpacing(theme::SPACE_SM);
        auto* t = new QLabel("QUÉT MÃ QR ĐỂ THANH TOÁN");
        t->setAlignment(Qt::AlignCenter);
        t->setStyleSheet(
            "color:#71717a;font-size:17px;font-weight:700;letter-spacing:2px;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(t);
        qr_widget_ = new QRWidget;
        // No alignment flag — widget expands to fill all available space.
        sv->addWidget(qr_widget_, 1);
        qr_section = sec;
    }

    // Bank list section — grid rows/cells share space via stretch so the
    // whole grid scales with the available height without clipping labels.
    {
        auto* sec = new QWidget;
        sec->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        auto* sv = new QVBoxLayout(sec);
        sv->setContentsMargins(theme::SPACE_SM, theme::SPACE_SM,
                                theme::SPACE_SM, theme::SPACE_SM);
        sv->setSpacing(4);
        auto* t = new QLabel("NGÂN HÀNG HỖ TRỢ");
        t->setStyleSheet(
            "color:#71717a;font-size:11px;font-weight:700;letter-spacing:1.5px;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(t);

        auto* grid_outer = new QWidget;
        grid_outer->setObjectName("bankGrid");
        grid_outer->setStyleSheet(
            QString("QWidget#bankGrid {background:%1;border:4px solid %2;}")
            .arg(theme::BG_WHITE, theme::HEAVY_BORDER));
        auto* grid_v = new QVBoxLayout(grid_outer);
        grid_v->setContentsMargins(0, 0, 0, 0);
        grid_v->setSpacing(0);

        constexpr int kCols = 4, kRows = 3;
        for (int row = 0; row < kRows; ++row) {
            auto* row_w = new QWidget;
            row_w->setStyleSheet("background:transparent;");
            auto* rh = new QHBoxLayout(row_w);
            rh->setContentsMargins(0, 0, 0, 0);
            rh->setSpacing(0);
            for (int col = 0; col < kCols; ++col) {
                int i = row * kCols + col;
                auto* cell = new QWidget;
                cell->setStyleSheet("background:#ffffff;");
                cell->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
                auto* cv2 = new QVBoxLayout(cell);
                cv2->setContentsMargins(4, 2, 4, 2);
                cv2->setAlignment(Qt::AlignCenter);
                cv2->setSpacing(0);

                auto* code_lbl = new QLabel(kBanks[i][0]);
                code_lbl->setAlignment(Qt::AlignCenter);
                code_lbl->setStyleSheet(
                    "color:#09090b;font-size:19px;font-weight:800;"
                    "background:transparent;"
                    "font-family:\"Helvetica Neue\",Arial;");
                cv2->addWidget(code_lbl);

                auto* name_lbl = new QLabel(kBanks[i][1]);
                name_lbl->setAlignment(Qt::AlignCenter);
                name_lbl->setWordWrap(false);
                name_lbl->setStyleSheet(
                    "color:#71717a;font-size:13px;font-weight:600;background:transparent;"
                    "font-family:\"Helvetica Neue\",Arial;");
                cv2->addWidget(name_lbl);
                rh->addWidget(cell, 1);
                if (col < kCols - 1) rh->addWidget(heavyVSep());
            }
            grid_v->addWidget(row_w, 1);
            if (row < kRows - 1) grid_v->addWidget(heavySep());
        }
        sv->addWidget(grid_outer, 1);
        bank_section = sec;
    }

    // Plate dark section
    {
        auto* sec = new QWidget;
        sec->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));
        auto* sv = new QVBoxLayout(sec);
        sv->setContentsMargins(theme::SPACE_MD, theme::SPACE_MD,
                                theme::SPACE_MD, theme::SPACE_MD);
        sv->setSpacing(theme::SPACE_SM);
        auto* t = new QLabel("BIỂN SỐ NHẬN DIỆN");
        t->setStyleSheet(
            "color:#71717a;font-size:14px;font-weight:700;letter-spacing:1.5px;"
            "background:transparent;font-family:\"Helvetica Neue\",Arial;");
        sv->addWidget(t);

        auto* dark = new QWidget;
        applyHeavyBox(dark, theme::BG_ZINC900);
        auto* dv = new QVBoxLayout(dark);
        dv->setContentsMargins(theme::SPACE_MD, theme::SPACE_SM,
                                theme::SPACE_MD, theme::SPACE_SM);
        dv->setAlignment(Qt::AlignCenter);
        lbl_plate_dark_ = new QLabel("---.--");
        lbl_plate_dark_->setAlignment(Qt::AlignCenter);
        lbl_plate_dark_->setStyleSheet(
            "color:#ffffff;font-size:50px;font-weight:800;letter-spacing:4px;"
            "background:transparent;"
            "font-family:\"Helvetica Neue\",Arial;");
        dv->addWidget(lbl_plate_dark_);
        sv->addWidget(dark, 1);
        plate_section = sec;
    }

    // ── Assemble the grid ─────────────────────────────────────────────
    // Row 0 (info ↔ QR)  : stretch 8 — synced across columns.
    // Row 1 (heavySep)   : fixed 4 px, one per column.
    // Row 2 (payment ↔ bank+plate): stretch 4 — also synced.
    body_g->addWidget(info_section,    0, 0);
    body_g->addWidget(heavySep(),      1, 0);
    body_g->addWidget(payment_section, 2, 0);

    body_g->addWidget(heavyVSep(),     0, 1, 3, 1);

    body_g->addWidget(qr_section,      0, 2);
    body_g->addWidget(heavySep(),      1, 2);

    auto* right_bottom = new QWidget;
    right_bottom->setStyleSheet(QString("background:%1;").arg(theme::BG_ZINC200));
    auto* rbv = new QVBoxLayout(right_bottom);
    rbv->setContentsMargins(0, 0, 0, 0);
    rbv->setSpacing(0);
    rbv->addWidget(bank_section, 2);
    rbv->addWidget(heavySep());
    rbv->addWidget(plate_section, 2);
    body_g->addWidget(right_bottom,    2, 2);
}

void ScreenExitHourly::setVehicleInfo(const VehicleInfo& vi) {
    current_vi_ = vi;
    if (lbl_plate_)      lbl_plate_->setText(vi.plate);
    if (lbl_plate_dark_) lbl_plate_dark_->setText(vi.plate);
    if (lbl_entry_) {
        lbl_entry_->setText(vi.entry_time.toString("dd/MM/yyyy"));
        if (lbl_entry_sub_) lbl_entry_sub_->setText(vi.entry_time.toString("HH:mm:ss"));
    }
    if (lbl_total_)     lbl_total_->setText(QString("%L1 \u20ab").arg(vi.amount_due));
    if (lbl_remaining_) lbl_remaining_->setText(QString("%L1 \u20ab").arg(vi.amount_due));
    if (qr_widget_ && !vi.qr_url.isEmpty()) qr_widget_->setUrl(vi.qr_url);
}

void ScreenExitHourly::updatePayment(quint32 received, quint32 remaining) {
    if (lbl_received_)  lbl_received_->setText(QString("%L1 \u20ab").arg(received));
    if (lbl_remaining_) lbl_remaining_->setText(QString("%L1 \u20ab").arg(remaining));
}
