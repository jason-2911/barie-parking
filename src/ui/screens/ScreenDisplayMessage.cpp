#include "ui/screens/ScreenDisplayMessage.h"

#include "ui/Theme.h"
#include "ui/screens/ScreenHelpers.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

using namespace ui;

ScreenDisplayMessage::ScreenDisplayMessage(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet(QString("background:%1;").arg(theme::BG_BODY));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    auto* banner = new QWidget;
    banner->setFixedHeight(88);
    auto* bannerLayout = new QHBoxLayout(banner);
    bannerLayout->setContentsMargins(0, 0, 0, 0);

    header_ = new QLabel("THÔNG BÁO HỆ THỐNG");
    header_->setAlignment(Qt::AlignCenter);
    header_->setStyleSheet(
        "color:#ffffff;font-size:36px;font-weight:900;"
        "background:transparent;font-family:\"Helvetica Neue\",Arial;");
    bannerLayout->addWidget(header_);
    root->addWidget(banner);
    root->addWidget(heavySep());

    auto* body = new QWidget;
    auto* bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(theme::SPACE_2XL, theme::SPACE_2XL,
                                   theme::SPACE_2XL, theme::SPACE_2XL);
    bodyLayout->setSpacing(theme::SPACE_XL);

    auto* card = new QWidget;
    applyHeavyBox(card, theme::BG_WHITE);
    auto* cardLayout = new QVBoxLayout(card);
    cardLayout->setContentsMargins(theme::SPACE_2XL, theme::SPACE_2XL,
                                   theme::SPACE_2XL, theme::SPACE_2XL);
    cardLayout->setSpacing(theme::SPACE_XL);

    title_ = new QLabel("THÔNG BÁO");
    title_->setAlignment(Qt::AlignCenter);
    title_->setStyleSheet(
        "color:#09090b;font-size:40px;font-weight:900;"
        "background:transparent;font-family:\"Helvetica Neue\",Arial;");
    cardLayout->addWidget(title_);

    message_ = new QLabel;
    message_->setAlignment(Qt::AlignCenter);
    message_->setWordWrap(true);
    message_->setStyleSheet(
        "color:#18181b;font-size:28px;font-weight:700;line-height:1.25;"
        "background:transparent;font-family:\"Helvetica Neue\",Arial;");
    cardLayout->addWidget(message_, 1);

    countdown_ = new QLabel;
    countdown_->setAlignment(Qt::AlignCenter);
    countdown_->setStyleSheet(
        "color:#52525b;font-size:20px;font-weight:800;"
        "background:transparent;font-family:\"Helvetica Neue\",Arial;");
    cardLayout->addWidget(countdown_);

    ack_button_ = makeActionBtn("ĐÃ HIỂU", theme::BG_ZINC900, "#ffffff");
    ack_button_->hide();
    connect(ack_button_, &QPushButton::clicked, this, &ScreenDisplayMessage::dismissed);
    cardLayout->addWidget(ack_button_);

    bodyLayout->addWidget(card, 1);
    root->addWidget(body, 1);

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &ScreenDisplayMessage::onTick);

    applySeverity("info");
}

void ScreenDisplayMessage::setMessage(const DisplayMessage& dm) {
    stop();

    message_->setText(dm.message);
    ack_button_->setVisible(dm.require_ack);
    remaining_ = dm.duration_seconds;
    applySeverity(dm.severity);

    if (remaining_ > 0) {
        countdown_->setText(QString("Tự đóng sau %1 giây").arg(remaining_));
        timer_->start();
    } else {
        countdown_->setText(dm.require_ack
            ? QStringLiteral("Nhấn nút bên dưới để tiếp tục")
            : QStringLiteral("Đang chờ thao tác tiếp theo"));
    }
}

void ScreenDisplayMessage::stop() {
    if (timer_->isActive())
        timer_->stop();
    remaining_ = 0;
}

void ScreenDisplayMessage::onTick() {
    if (remaining_ <= 0) {
        timer_->stop();
        emit dismissed();
        return;
    }

    --remaining_;
    if (remaining_ > 0) {
        countdown_->setText(QString("Tự đóng sau %1 giây").arg(remaining_));
        return;
    }

    timer_->stop();
    emit dismissed();
}

void ScreenDisplayMessage::applySeverity(const QString& severity) {
    QString bg = theme::BG_ZINC900;
    QString title = "THÔNG BÁO HỆ THỐNG";

    if (severity == "warning") {
        bg = theme::ORANGE;
        title = "CẢNH BÁO";
    } else if (severity == "error") {
        bg = theme::DANGER;
        title = "LỖI HỆ THỐNG";
    } else if (severity == "success") {
        bg = theme::GREEN_HEADER;
        title = "HOÀN TẤT";
    }

    header_->setText(title);
    header_->parentWidget()->setStyleSheet(QString("background:%1;").arg(bg));
    title_->setText(title);
}
