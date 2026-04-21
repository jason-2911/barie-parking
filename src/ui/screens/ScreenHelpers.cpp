#include "ui/screens/ScreenHelpers.h"
#include <QHBoxLayout>

namespace ui {

QWidget* makePlateWidget(const QString& plate, QLabel** out_label) {
    auto* w = new QWidget;
    applyHeavyBox(w, theme::PLATE_BG);

    auto* h = new QHBoxLayout(w);
    h->setContentsMargins(18, 18, 18, 18);
    h->setSpacing(0);

    auto* flag = new QFrame;
    flag->setMinimumWidth(6);
    flag->setMaximumWidth(6);
    flag->setStyleSheet(
        "background:qlineargradient(x1:0,y1:0,x2:0,y2:1,"
        "stop:0 #da251d,stop:0.5 #da251d,"
        "stop:0.5 #003087,stop:1 #003087);"
        "border:none;");
    h->addWidget(flag);
    h->addSpacing(12);

    auto* lbl = new QLabel(plate);
    lbl->setAlignment(Qt::AlignVCenter | Qt::AlignLeft);
    lbl->setStyleSheet(
        QString("color:%1;font-size:40px;font-weight:800;"
                "letter-spacing:2px;background:transparent;border:none;"
                "padding:6px 0;"
                "font-family:\"Helvetica Neue\",Arial;")
        .arg(theme::PLATE_YELLOW));
    h->addWidget(lbl, 1);

    if (out_label) *out_label = lbl;
    return w;
}

} // namespace ui
