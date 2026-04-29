#include "ui/screens/ScreenPaymentFailed.h"
#include "ui/widgets/TitleBar.h"
#include "ui/Theme.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QPainter>
#include <QGraphicsDropShadowEffect>

namespace {

class CrossBadge : public QWidget {
public:
    explicit CrossBadge(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(130, 130);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = rect().adjusted(4, 4, -4, -4);
        p.setBrush(QColor(theme::V2_DANGER_SOFT));
        p.setPen(QPen(QColor(theme::V2_DANGER), 8));
        p.drawEllipse(r);

        QPen x(QColor(theme::V2_DANGER), 10, Qt::SolidLine,
                Qt::RoundCap, Qt::RoundJoin);
        p.setPen(x);
        p.setBrush(Qt::NoBrush);

        const QPointF c = rect().center();
        const qreal s = rect().width() * 0.22;
        p.drawLine(QPointF(c.x() - s, c.y() - s),
                    QPointF(c.x() + s, c.y() + s));
        p.drawLine(QPointF(c.x() - s, c.y() + s),
                    QPointF(c.x() + s, c.y() - s));
    }
};

QGraphicsDropShadowEffect* makeCardShadow(QObject* parent) {
    auto* eff = new QGraphicsDropShadowEffect(parent);
    eff->setBlurRadius(28);
    eff->setOffset(0, 10);
    eff->setColor(QColor(0, 0, 0, 36));
    return eff;
}

} // anonymous namespace

ScreenPaymentFailed::ScreenPaymentFailed(QWidget* parent)
    : QWidget(parent)
{
    setStyleSheet(QString("background:%1;").arg(theme::V2_BG_PAGE));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    root->addWidget(new TitleBar(TitleBar::Variant::Failed, this));

    auto* body = new QWidget;
    body->setStyleSheet(QString("background:%1;").arg(theme::V2_BG_PAGE));
    auto* body_v = new QVBoxLayout(body);
    body_v->setContentsMargins(52, 68, 52, 32);
    body_v->setSpacing(0);
    root->addWidget(body, 1);

    auto* card = new QWidget;
    card->setObjectName("failedCard");
    card->setStyleSheet(
        QString("QWidget#failedCard {background:%1;border-radius:24px;}")
        .arg(theme::BG_WHITE));
    card->setGraphicsEffect(makeCardShadow(card));
    card->setMinimumHeight(470);

    auto* cv = new QVBoxLayout(card);
    cv->setContentsMargins(40, 40, 40, 40);
    cv->setSpacing(0);
    cv->setAlignment(Qt::AlignCenter);

    cv->addStretch();

    auto* badge = new CrossBadge(card);
    cv->addWidget(badge, 0, Qt::AlignHCenter);
    cv->addSpacing(48);

    auto* main = new QLabel("THANH TOÁN THẤT BẠI");
    main->setAlignment(Qt::AlignCenter);
    main->setStyleSheet(
        QString("color:%1;font-size:42px;font-weight:800;background:transparent;"
                "font-family:\"Inter\",\"Helvetica Neue\",Arial;")
        .arg(theme::V2_DANGER));
    cv->addWidget(main);

    cv->addSpacing(12);

    lbl_subtext_ = new QLabel("Tự động reset màn hình trong 5s");
    lbl_subtext_->setAlignment(Qt::AlignCenter);
    lbl_subtext_->setStyleSheet(
        QString("color:%1;font-size:24px;font-weight:700;background:transparent;"
                "font-family:\"Inter\",\"Helvetica Neue\",Arial;")
        .arg(theme::V2_TEXT_PLACEHOLDER));
    cv->addWidget(lbl_subtext_);

    cv->addStretch();

    body_v->addWidget(card, 1);

    timer_ = new QTimer(this);
    timer_->setInterval(1000);
    connect(timer_, &QTimer::timeout, this, &ScreenPaymentFailed::onTick);
}

void ScreenPaymentFailed::startCountdown(int seconds) {
    remaining_ = qMax(1, seconds);
    if (lbl_subtext_)
        lbl_subtext_->setText(
            QString("Tự động reset màn hình trong %1s").arg(remaining_));
    timer_->start();
}

void ScreenPaymentFailed::stopCountdown() {
    if (timer_) timer_->stop();
}

void ScreenPaymentFailed::onTick() {
    --remaining_;
    if (lbl_subtext_)
        lbl_subtext_->setText(
            QString("Tự động reset màn hình trong %1s")
                .arg(qMax(0, remaining_)));
    if (remaining_ <= 0) {
        timer_->stop();
        emit countdownExpired();
    }
}
