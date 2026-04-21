/**
 * ScreenHelpers.h — Neo-brutalist UI helpers.
 */

#pragma once
#include <QWidget>
#include <QLabel>
#include <QPushButton>
#include <QFrame>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include "ui/Theme.h"

namespace ui {

// ── Horizontal heavy separator (4 px black) ───────────────────────────────────
inline QFrame* heavySep() {
    auto* f = new QFrame;
    f->setFixedHeight(4);
    f->setStyleSheet(
        QString("background:%1;border:none;").arg(theme::HEAVY_BORDER));
    f->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    return f;
}

// ── Vertical heavy separator (4 px black) ────────────────────────────────────
inline QFrame* heavyVSep() {
    auto* f = new QFrame;
    f->setFixedWidth(4);
    f->setStyleSheet(
        QString("background:%1;border:none;").arg(theme::HEAVY_BORDER));
    f->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Expanding);
    return f;
}

// ── Section label (small caps, zinc-500) ─────────────────────────────────────
inline QLabel* makeLabel(const QString& text,
                          int            size    = 14,
                          const QString& color   = theme::TEXT_DARK,
                          bool           bold    = false,
                          const QString& extra   = "")
{
    auto* l = new QLabel(text);
    QString s = QString("color:%1;font-size:%2px;background:transparent;font-family:\"Helvetica Neue\",Arial;")
                .arg(color).arg(size);
    if (bold) s += "font-weight:900;";
    if (!extra.isEmpty()) s += extra;
    l->setStyleSheet(s);
    return l;
}

inline QLabel* makeSectionTitle(const QString& text) {
    return makeLabel(text.toUpper(), 13, theme::TEXT_MUTED, true,
                     "letter-spacing:1.5px;");
}

inline QFrame* makeSeparator() { return heavySep(); }

// ── Action button (full-width, no radius) ────────────────────────────────────
inline QPushButton* makeActionBtn(const QString& text,
                                   const QString& bg    = theme::BG_ZINC900,
                                   const QString& color = "#ffffff")
{
    auto* b = new QPushButton(text);
    b->setFixedHeight(72);
    b->setStyleSheet(
        QString("QPushButton{background:%1;color:%2;border:none;border-radius:0;"
                "font-size:18px;font-weight:900;letter-spacing:1px;"
                "font-family:\"Helvetica Neue\",Arial;}"
                "QPushButton:hover{opacity:0.9;}")
        .arg(bg, color));
    return b;
}

inline QPushButton* makeSupportBtn() {
    auto* b = makeActionBtn("GỌI HỖ TRỢ", theme::DANGER, "#ffffff");
    b->setObjectName("btnSupport");
    return b;
}

// ── Info tile: label on top, large value below ────────────────────────────────
inline QWidget* makeInfoTile(const QString& label,
                               const QString& value,
                               QLabel**       val_out,
                               int            val_size = 40,
                               const QString& bg       = theme::BG_WHITE,
                               const QString& val_color= theme::TEXT_DARK)
{
    auto* w = new QWidget;
    w->setStyleSheet(QString("background:%1;").arg(bg));
    auto* v = new QVBoxLayout(w);
    v->setContentsMargins(theme::SPACE_MD, theme::SPACE_MD,
                           theme::SPACE_MD, theme::SPACE_MD);
    v->setSpacing(theme::SPACE_SM);

    auto* t = makeLabel(label, 13, theme::TEXT_MUTED, true, "letter-spacing:1.5px;");
    v->addWidget(t);

    auto* vl = makeLabel(value, val_size, val_color, true);
    if (val_out) *val_out = vl;
    v->addWidget(vl);
    v->addStretch();
    return w;
}

// ── VN license plate widget ──────────────────────────────────────────────────
QWidget* makePlateWidget(const QString& plate, QLabel** out_label = nullptr);

// ── Scoped heavy box: 4px black border that does NOT cascade to children ─────
inline void applyHeavyBox(QWidget* w, const QString& bg = theme::BG_WHITE,
                            int width = 4) {
    static int counter = 0;
    QString name = QString("hbox_%1").arg(++counter);
    w->setObjectName(name);
    w->setStyleSheet(
        QString("QWidget#%1 {background:%2;border:%3px solid %4;}")
        .arg(name, bg).arg(width).arg(theme::HEAVY_BORDER));
}

// ── Compat wrappers ───────────────────────────────────────────────────────────
inline QWidget* makeCard(const QString& bg     = theme::BG_WHITE,
                          const QString&        = theme::HEAVY_BORDER,
                          int                   = 0)
{
    auto* w = new QWidget;
    w->setStyleSheet(QString("background:%1;").arg(bg));
    return w;
}

inline QWidget* makeInfoRow(const QString& label,
                              const QString& value,
                              const QString& val_color = theme::TEXT_DARK,
                              QLabel**       out       = nullptr)
{
    auto* w = new QWidget;
    w->setStyleSheet("background:transparent;");
    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(theme::SPACE_SM);
    auto* l = makeLabel(label, 12, theme::TEXT_MUTED);
    l->setMinimumWidth(120);
    h->addWidget(l);
    auto* vl = makeLabel(value, 14, val_color, true);
    if (out) *out = vl;
    h->addWidget(vl, 1);
    return w;
}

} // namespace ui
