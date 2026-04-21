#include "ui/widgets/BottomBar.h"
#include "ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QFrame>

static QString dotStyle(const QString& color) {
    return QString("background:%1;border-radius:3px;min-width:7px;max-width:7px;"
                   "min-height:7px;max-height:7px;").arg(color);
}

BottomBar::BottomBar(const Config& cfg, QWidget* parent)
    : QWidget(parent)
{
    setFixedHeight(36);
    setStyleSheet(
        QString("background:%1;border-top:1px solid %2;")
        .arg(theme::BG_ELEVATED, theme::BORDER_SUBTLE));

    auto* h = new QHBoxLayout(this);
    h->setContentsMargins(theme::SPACE_LG, 0, theme::SPACE_LG, 0);
    h->setSpacing(theme::SPACE_MD);

    auto addField = [&](QLabel*& dot_out, QLabel*& lbl_out,
                        const QString& text, const QString& dot_color) {
        auto* w = new QWidget;
        w->setStyleSheet("background:transparent;");
        auto* fh = new QHBoxLayout(w);
        fh->setContentsMargins(0, 0, 0, 0);
        fh->setSpacing(6);

        dot_out = new QLabel;
        dot_out->setStyleSheet(dotStyle(dot_color));
        fh->addWidget(dot_out);

        lbl_out = new QLabel(text);
        lbl_out->setStyleSheet(
            QString("color:%1;font-size:11px;background:transparent;")
            .arg(theme::TEXT_MUTED));
        fh->addWidget(lbl_out);
        h->addWidget(w);
    };

    addField(dot_mqtt_,    lbl_mqtt_,    "MQTT",    theme::TEXT_DIMMED);

    auto* sep = new QFrame;
    sep->setFrameShape(QFrame::VLine);
    sep->setFixedWidth(1);
    sep->setStyleSheet(
        QString("background:%1;border:none;margin:9px 0;").arg(theme::BORDER_SUBTLE));
    h->addWidget(sep);

    addField(dot_barrier_, lbl_barrier_, "Barrier", theme::TEXT_DIMMED);

    lbl_msg_ = new QLabel;
    lbl_msg_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    lbl_msg_->setAlignment(Qt::AlignCenter);
    lbl_msg_->setStyleSheet(
        QString("color:%1;font-size:11px;background:transparent;")
        .arg(theme::TEXT_MUTED));
    h->addWidget(lbl_msg_, 1);

    lbl_device_ = new QLabel(cfg.device.id);
    lbl_device_->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    lbl_device_->setStyleSheet(
        QString("color:%1;font-size:10px;font-family:monospace;"
                "background:transparent;").arg(theme::TEXT_DIMMED));
    h->addWidget(lbl_device_);
}

void BottomBar::setMqttStatus(bool connected) {
    dot_mqtt_->setStyleSheet(dotStyle(connected ? theme::SUCCESS : theme::DANGER));
    lbl_mqtt_->setStyleSheet(
        QString("color:%1;font-size:11px;background:transparent;")
        .arg(connected ? theme::TEXT_SECONDARY : theme::TEXT_MUTED));
    lbl_mqtt_->setText(connected ? "MQTT: Online" : "MQTT: Offline");
}

void BottomBar::setBarrierStatus(const QString& id, const QString& pos) {
    const bool up = (pos == "up");
    dot_barrier_->setStyleSheet(dotStyle(up ? theme::SUCCESS : theme::TEXT_DIMMED));
    lbl_barrier_->setText(QString("%1: %2").arg(id, pos.toUpper()));
}

void BottomBar::setMessage(const QString& msg) {
    lbl_msg_->setText(msg);
}
