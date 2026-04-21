#include "ui/screens/ScreenEntryIdle.h"
#include "ui/screens/ScreenHelpers.h"
#include "ui/Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>

using namespace ui;

// ── Shared helpers ────────────────────────────────────────────────────────────
static QString labelStyle(int size, const QString& color = "#09090b",
                           bool bold = false, const QString& extra = "") {
    QString s = QString("color:%1;font-size:%2px;background:transparent;"
                        "font-family:\"Helvetica Neue\",Arial;")
                .arg(color).arg(size);
    if (bold) s += "font-weight:900;";
    if (!extra.isEmpty()) s += extra;
    return s;
}

static QWidget* makeStepCard(const QString& number, const QString& title,
                              const QString& desc, bool = false) {
    auto* w = new QWidget;
    w->setStyleSheet(QString("background:%1;").arg(theme::BG_WHITE));

    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(theme::SPACE_LG, theme::SPACE_LG,
                           theme::SPACE_LG, theme::SPACE_LG);
    v->setSpacing(theme::SPACE_MD);

    // Numbered icon box
    auto* icon_box = new QWidget;
    icon_box->setFixedSize(64, 64);
    applyHeavyBox(icon_box, theme::ORANGE);
    auto* num_lbl = new QLabel(number, icon_box);
    num_lbl->setAlignment(Qt::AlignCenter);
    num_lbl->resize(64, 64);
    num_lbl->setStyleSheet(
        "color:#ffffff;font-size:34px;font-weight:900;background:transparent;"
        "font-family:\"Helvetica Neue\",Arial;");
    v->addWidget(icon_box);

    auto* t = new QLabel(title);
    t->setStyleSheet(labelStyle(20, theme::TEXT_DARK, true));
    v->addWidget(t);

    auto* d = new QLabel(desc);
    d->setWordWrap(true);
    d->setMinimumHeight(72);
    d->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    d->setStyleSheet(labelStyle(15, theme::TEXT_GRAY, false));
    v->addWidget(d, 1);
    return w;
}

// ── Screen ────────────────────────────────────────────────────────────────────
ScreenEntryIdle::ScreenEntryIdle(const Config& cfg, QWidget* parent)
    : QWidget(parent), cfg_(cfg)
{
    setStyleSheet(QString("background:%1;").arg(theme::BG_BODY));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Orange banner ─────────────────────────────────────────────────
    auto* banner = new QWidget;
    banner->setStyleSheet(
        QString("background:%1;").arg(theme::ORANGE));
    banner->setFixedHeight(72);
    auto* bh = new QHBoxLayout(banner);
    bh->setContentsMargins(0, 0, 0, 0);
    auto* banner_lbl = new QLabel("MỜI QUÉT VÉ ĐỂ THANH TOÁN");
    banner_lbl->setAlignment(Qt::AlignCenter);
    banner_lbl->setStyleSheet(labelStyle(38, "#ffffff", true, "letter-spacing:2px;"));
    bh->addWidget(banner_lbl);
    root->addWidget(banner);
    root->addWidget(heavySep());

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

    // section: no-ticket info
    auto* top_sec = new QWidget;
    top_sec->setStyleSheet(
        QString("background:%1;").arg(theme::BG_WHITE));
    auto* tsv = new QVBoxLayout(top_sec);
    tsv->setContentsMargins(theme::SPACE_XL, theme::SPACE_XL,
                              theme::SPACE_XL, theme::SPACE_XL);
    tsv->setAlignment(Qt::AlignCenter);
    tsv->setSpacing(theme::SPACE_MD);

    tsv->addStretch();

    auto* no_tx_lbl = new QLabel("CHƯA CÓ GIAO DỊCH");
    no_tx_lbl->setAlignment(Qt::AlignCenter);
    no_tx_lbl->setStyleSheet(labelStyle(42, theme::TEXT_DARK, true));
    tsv->addWidget(no_tx_lbl);

    auto* sub_lbl = new QLabel("Vui lòng đưa vé vào khay quét phía trước máy");
    sub_lbl->setAlignment(Qt::AlignCenter);
    sub_lbl->setStyleSheet(labelStyle(20, theme::TEXT_GRAY, false));
    tsv->addWidget(sub_lbl);

    tsv->addStretch();

    lv->addWidget(top_sec);
    lv->addWidget(heavySep());

    // section: 3 step cards
    auto* steps_sec = new QWidget;
    steps_sec->setStyleSheet(
        QString("background:%1;").arg(theme::BG_WHITE));
    auto* ssh = new QHBoxLayout(steps_sec);
    ssh->setContentsMargins(0, 0, 0, 0);
    ssh->setSpacing(0);
    ssh->addWidget(makeStepCard("1", "QUÉT VÉ",
        "Di chuyển vào vị trí chỉ định.", false), 1);
    ssh->addWidget(heavyVSep());
    ssh->addWidget(makeStepCard("2", "THANH TOÁN",
        "Tiền mặt hoặc quét QR.", false), 1);
    ssh->addWidget(heavyVSep());
    ssh->addWidget(makeStepCard("3", "RA CỔNG",
        "Di chuyển ra cổng khi xong.", false), 1);
    lv->addWidget(steps_sec);
    lv->addWidget(heavySep());

    // section: dark waiting banner (fills rest)
    auto* wait_sec = new QWidget;
    wait_sec->setStyleSheet(
        QString("background:%1;").arg(theme::BG_ZINC900));
    auto* wv = new QVBoxLayout(wait_sec);
    wv->setContentsMargins(theme::SPACE_XL, theme::SPACE_2XL,
                            theme::SPACE_XL, theme::SPACE_2XL);
    wv->setAlignment(Qt::AlignCenter);
    wv->setSpacing(theme::SPACE_MD);

    auto* sys_lbl = new QLabel("TRẠNG THÁI HỆ THỐNG");
    sys_lbl->setAlignment(Qt::AlignCenter);
    sys_lbl->setStyleSheet(labelStyle(20, "rgba(255,255,255,0.6)", true));
    wv->addWidget(sys_lbl);

    auto* wait_lbl = new QLabel("ĐANG CHỜ XE VÀO");
    wait_lbl->setAlignment(Qt::AlignCenter);
    wait_lbl->setStyleSheet(labelStyle(48, "#ffffff", true, "letter-spacing:4px;"));
    wv->addWidget(wait_lbl);

    lv->addWidget(wait_sec, 1);

    body_h->addWidget(left, 55);
    body_h->addWidget(heavyVSep());

    // ── Right column (45%) ────────────────────────────────────────────
    auto* right = new QWidget;
    right->setStyleSheet(
        QString("background:%1;").arg(theme::BG_ZINC200));
    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(0, 0, 0, 0);
    rv->setSpacing(0);

    // main area: car + ready text (fills)
    auto* ready_sec = new QWidget;
    ready_sec->setStyleSheet(QString("background:%1;").arg(theme::BG_ZINC200));
    auto* rsv = new QVBoxLayout(ready_sec);
    rsv->setContentsMargins(theme::SPACE_XL, theme::SPACE_2XL,
                              theme::SPACE_XL, theme::SPACE_2XL);
    rsv->setAlignment(Qt::AlignCenter);
    rsv->setSpacing(theme::SPACE_XL);

    auto* car_ring = new QWidget;
    car_ring->setFixedSize(160, 160);
    car_ring->setObjectName("carRing");
    car_ring->setStyleSheet(
        QString("QWidget#carRing {background:%1;border:4px solid %2;"
                "border-radius:80px;}")
        .arg(theme::ORANGE, theme::HEAVY_BORDER));
    auto* car_lbl = new QLabel("24/7", car_ring);
    car_lbl->setAlignment(Qt::AlignCenter);
    car_lbl->resize(160, 160);
    car_lbl->setStyleSheet(
        "color:#ffffff;font-size:48px;font-weight:900;background:transparent;"
        "font-family:\"Helvetica Neue\",Arial;letter-spacing:2px;");
    rsv->addWidget(car_ring, 0, Qt::AlignHCenter);

    auto* ready_lbl = new QLabel("SẴN SÀNG PHỤC VỤ 24/7");
    ready_lbl->setAlignment(Qt::AlignCenter);
    ready_lbl->setStyleSheet(labelStyle(28, theme::TEXT_DARK, true));
    rsv->addWidget(ready_lbl);

    // auto* ready_sub = new QLabel("Giao diện chờ, rõ nét, dễ nhìn từ xa");
    // ready_sub->setAlignment(Qt::AlignCenter);
    // ready_sub->setStyleSheet(labelStyle(18, theme::TEXT_GRAY));
    // rsv->addWidget(ready_sub);

    rv->addWidget(ready_sec, 1);

    body_h->addWidget(right, 45);
}
