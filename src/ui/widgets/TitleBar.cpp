#include "ui/widgets/TitleBar.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>

TitleBar::TitleBar(Variant v, QWidget* parent)
    : QWidget(parent), variant_(v)
{
    setFixedHeight(78);
    // Plain QWidget needs WA_StyledBackground for stylesheet `background:`
    // to actually paint; without it the page-grey shows through and the
    // white title text becomes invisible.
    setAttribute(Qt::WA_StyledBackground, true);

    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(0);

    lbl_ = new QLabel(this);
    lbl_->setAlignment(Qt::AlignCenter);
    h->addWidget(lbl_, 1);

    apply();
}

void TitleBar::setVariant(Variant v) {
    if (variant_ == v) return;
    variant_ = v;
    apply();
}

void TitleBar::setText(const QString& text) {
    if (lbl_) lbl_->setText(text);
}

void TitleBar::apply() {
    QString bg, text;
    switch (variant_) {
        case Variant::Success:
            bg   = theme::V2_TITLE_GREEN;
            text = "THANH TOÁN THÀNH CÔNG";
            break;
        case Variant::Failed:
            bg   = theme::V2_TITLE_RED;
            text = "THANH TOÁN THẤT BẠI";
            break;
        case Variant::Payment:
        default:
            bg   = theme::V2_TITLE_RED;
            text = "THANH TOÁN PHÍ GỬI XE";
            break;
    }
    setStyleSheet(QString("background:%1;").arg(bg));
    lbl_->setText(text);
    lbl_->setStyleSheet(
        "color:#FFFFFF;font-size:36px;font-weight:800;letter-spacing:1px;"
        "background:transparent;text-transform:uppercase;"
        "font-family:\"Inter\",\"Helvetica Neue\",Arial;");
}
