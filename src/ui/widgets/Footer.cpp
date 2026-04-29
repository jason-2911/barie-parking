#include "ui/widgets/Footer.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>

namespace {

// Logical (DPI-independent) logo height. The PNG is loaded at native size,
// then smooth-scaled down to this height while preserving aspect ratio.
constexpr int kLogoHeight = 28;

QLabel* makeLogoLabel(QWidget* parent) {
    auto* lbl = new QLabel(parent);
    lbl->setStyleSheet("background:transparent;");
    lbl->setAttribute(Qt::WA_TransparentForMouseEvents, true);

    QPixmap pm(":/images/logo.png");
    if (!pm.isNull()) {
        const QPixmap scaled = pm.scaledToHeight(
            kLogoHeight, Qt::SmoothTransformation);
        lbl->setPixmap(scaled);
        lbl->setFixedSize(scaled.size());
    } else {
        lbl->setText("VioTech");
        lbl->setStyleSheet(
            "color:#008F39;font-size:14px;font-weight:800;background:transparent;");
        lbl->setFixedHeight(kLogoHeight);
    }
    return lbl;
}

} // anonymous namespace

Footer::Footer(QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(48);
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString("background:%1;").arg(theme::V2_BG_PAGE));

    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(56, 0, 56, 0);
    h->setSpacing(0);

    auto* logo = makeLogoLabel(this);
    h->addWidget(logo, 0, Qt::AlignVCenter | Qt::AlignLeft);

    auto* center = new QLabel("Power by TNHH Công Nghệ VioTech Company");
    center->setAlignment(Qt::AlignCenter);
    center->setStyleSheet(
        QString("color:%1;font-size:16px;font-weight:500;background:transparent;"
                "font-family:\"Inter\",\"Helvetica Neue\",Arial;")
        .arg(theme::V2_TEXT_MUTED));
    h->addWidget(center, 1);

    // Spacer that mirrors the logo width so the centre label stays
    // visually centred regardless of logo aspect ratio.
    auto* spacer = new QWidget(this);
    spacer->setFixedSize(logo->width(), kLogoHeight);
    spacer->setStyleSheet("background:transparent;");
    h->addWidget(spacer, 0, Qt::AlignVCenter | Qt::AlignRight);
}
