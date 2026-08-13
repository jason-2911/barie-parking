#include "ui/screens/ScreenExitHourly.h"
#include "ui/widgets/QRWidget.h"
#include "ui/widgets/TitleBar.h"
#include "ui/Theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QLabel>
#include <QFrame>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsOpacityEffect>
#include <QTimer>
#include <QPainter>

namespace {

// ── Layout / behaviour constants ─────────────────────────────────────────────
constexpr int kWarningSeconds      = 30;
constexpr int kTimedOutHoldSeconds = 5;

// Soft drop shadow shared between left and right cards.
QGraphicsDropShadowEffect* makeCardShadow(QObject* parent) {
    auto* eff = new QGraphicsDropShadowEffect(parent);
    eff->setBlurRadius(28);
    eff->setOffset(0, 10);
    eff->setColor(QColor(0, 0, 0, 36));
    return eff;
}

// Refresh-icon overlay shown over the QR when the session times out.
// White circle with a 4 px navy border and a "↻" glyph in the middle —
// matches the design without hand-rolling vector arrow heads.
class RefreshBadge : public QWidget {
public:
    explicit RefreshBadge(QWidget* parent = nullptr) : QWidget(parent) {
        setFixedSize(82, 82);
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }

protected:
    void paintEvent(QPaintEvent*) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing, true);

        const QRectF r = rect().adjusted(2, 2, -2, -2);
        p.setBrush(QColor(theme::BG_WHITE));
        p.setPen(QPen(QColor(theme::V2_TEXT_LABEL), 4));
        p.drawEllipse(r);

        QFont f("Inter");
        f.setStyleHint(QFont::SansSerif);
        f.setPixelSize(38);
        f.setWeight(QFont::Black);
        p.setFont(f);
        p.setPen(QColor(theme::V2_TEXT_LABEL));
        p.drawText(rect(), Qt::AlignCenter, QStringLiteral(u"↻"));
    }
};

QLabel* makeLabel(const QString& text, int size, int weight,
                  const QString& color, Qt::Alignment a = Qt::AlignLeft)
{
    auto* l = new QLabel(text);
    l->setAlignment(a);
    l->setStyleSheet(
        QString("color:%1;font-size:%2px;font-weight:%3;background:transparent;"
                "font-family:\"Inter\",\"Helvetica Neue\",Arial;")
        .arg(color).arg(size).arg(weight));
    return l;
}

QString currencyText(quint32 v) {
    return QString("%L1 VNĐ").arg(v);
}

QString formatDuration(int minutes) {
    int h = minutes / 60;
    int m = minutes % 60;
    return QString("%1 giờ %2 phút")
            .arg(h, 2, 10, QChar('0'))
            .arg(m, 2, 10, QChar('0'));
}

} // anonymous namespace

ScreenExitHourly::ScreenExitHourly(const Config& cfg, QWidget* parent)
    : QWidget(parent), cfg_(cfg)
{
    // Scale everything proportionally to the configured resolution.
    // Baseline design is 1920×1080; use the smaller axis ratio so nothing clips.
    const double scale = qMin(cfg_.ui.width  / 1920.0,
                              cfg_.ui.height / 1080.0);
    auto sc = [&](int v) { return qRound(v * scale); };

    msg_font_px_   = sc(26);
    timer_font_px_ = sc(42);

    setStyleSheet(QString("background:%1;").arg(theme::V2_BG_PAGE));

    auto* root = new QVBoxLayout(this);
    root->setContentsMargins(0, 0, 0, 0);
    root->setSpacing(0);

    // ── Title bar ─────────────────────────────────────────────────────────
    auto* title = new TitleBar(TitleBar::Variant::Payment, this);
    title->setFixedHeight(sc(78));
    root->addWidget(title);

    // ── Body ──────────────────────────────────────────────────────────────
    auto* body = new QWidget;
    body->setStyleSheet(QString("background:%1;").arg(theme::V2_BG_PAGE));
    auto* body_grid = new QGridLayout(body);
    body_grid->setContentsMargins(sc(52), sc(26), sc(52), sc(26));
    body_grid->setHorizontalSpacing(sc(26));
    body_grid->setVerticalSpacing(0);
    body_grid->setColumnStretch(0, 0);
    body_grid->setColumnStretch(1, 1);
    root->addWidget(body, 1);

    const QString card_sheet = QString("background:%1;border-radius:%2px;")
                                .arg(theme::BG_WHITE).arg(sc(24));

    // ════════════════════════════════════════════════════════════════════
    // LEFT CARD — vehicle / payment info
    // ════════════════════════════════════════════════════════════════════
    auto* left = new QWidget;
    left->setObjectName("leftCard");
    left->setFixedWidth(sc(445));
    left->setStyleSheet(QString("QWidget#leftCard {%1}").arg(card_sheet));
    left->setGraphicsEffect(makeCardShadow(left));

    auto* lv = new QVBoxLayout(left);
    lv->setContentsMargins(sc(52), sc(48), sc(52), sc(48));
    lv->setSpacing(0);

    auto addLabelValue = [&](const QString& label_text,
                              const QString& value_text,
                              QLabel** value_out,
                              const QString& value_color,
                              int value_size,
                              int top_margin) {
        if (top_margin > 0) lv->addSpacing(sc(top_margin));
        lv->addWidget(makeLabel(label_text, sc(24), 700, theme::V2_TEXT_LABEL));
        lv->addSpacing(sc(8));
        auto* v = makeLabel(value_text, sc(value_size), 800, value_color);
        if (value_out) *value_out = v;
        lv->addWidget(v);
    };

    addLabelValue("BIỂN SỐ XE",  "---.--", &lbl_plate_,
                  theme::V2_TEXT_DARK, 38, 0);
    addLabelValue("SỐ TIỀN CẦN THANH TOÁN", "0 VNĐ", &lbl_amount_due_,
                  theme::V2_SUCCESS, 36, 28);
    addLabelValue("SỐ TIỀN ĐÃ NẠP", "0 VNĐ", &lbl_amount_paid_,
                  theme::V2_DANGER, 36, 28);
    addLabelValue("SỐ TIỀN CÒN THIẾU", "0 VNĐ", &lbl_amount_remain_,
                  theme::V2_TEXT_DARK, 36, 28);

    lv->addSpacing(sc(28));
    auto* divider = new QFrame;
    divider->setFixedHeight(2);
    divider->setStyleSheet(QString("background:%1;border:none;")
                            .arg(theme::V2_DIVIDER));
    lv->addWidget(divider);
    lv->addSpacing(sc(24));

    // Time row — two columns (entry / exit)
    auto* time_row = new QWidget;
    time_row->setStyleSheet("background:transparent;");
    auto* tr_grid = new QGridLayout(time_row);
    tr_grid->setContentsMargins(0, 0, 0, 0);
    tr_grid->setHorizontalSpacing(sc(24));
    tr_grid->setVerticalSpacing(sc(4));
    tr_grid->setColumnStretch(0, 1);
    tr_grid->setColumnStretch(1, 1);

    tr_grid->addWidget(makeLabel("GIỜ VÀO", sc(22), 700, theme::V2_TEXT_LABEL), 0, 0);
    tr_grid->addWidget(makeLabel("GIỜ RA",  sc(22), 700, theme::V2_TEXT_LABEL), 0, 1);

    lbl_entry_time_ = makeLabel("--:--:--", sc(26), 800, theme::V2_TEXT_DARK);
    lbl_exit_time_  = makeLabel("--:--:--", sc(26), 800, theme::V2_TEXT_DARK);
    tr_grid->addWidget(lbl_entry_time_, 1, 0);
    tr_grid->addWidget(lbl_exit_time_,  1, 1);

    lbl_entry_date_ = makeLabel("--/--/----", sc(20), 600, theme::V2_TEXT_MUTED);
    lbl_exit_date_  = makeLabel("--/--/----", sc(20), 600, theme::V2_TEXT_MUTED);
    tr_grid->addWidget(lbl_entry_date_, 2, 0);
    tr_grid->addWidget(lbl_exit_date_,  2, 1);

    lv->addWidget(time_row);

    lv->addSpacing(sc(28));
    lv->addWidget(makeLabel("TỔNG THỜI GIAN", sc(22), 700, theme::V2_TEXT_LABEL));
    lv->addSpacing(sc(12));
    lbl_duration_ = makeLabel("00 giờ 00 phút", sc(28), 800, theme::V2_TEXT_DARK);
    lv->addWidget(lbl_duration_);

    lv->addStretch();

    body_grid->addWidget(left, 0, 0);

    // ════════════════════════════════════════════════════════════════════
    // RIGHT CARD — message + countdown + QR
    // ════════════════════════════════════════════════════════════════════
    auto* right = new QWidget;
    right->setObjectName("rightCard");
    right->setStyleSheet(QString("QWidget#rightCard {%1}").arg(card_sheet));
    right->setGraphicsEffect(makeCardShadow(right));

    auto* rv = new QVBoxLayout(right);
    rv->setContentsMargins(sc(56), sc(48), sc(56), sc(40));
    rv->setSpacing(0);

    lbl_message_ = makeLabel(
        "Quý khách có thể bỏ tiền ở ngăn dưới\nhoặc quét QR Code",
        msg_font_px_, 800, theme::V2_SUCCESS, Qt::AlignCenter);
    lbl_message_->setWordWrap(true);
    rv->addWidget(lbl_message_);
    rv->addSpacing(sc(16));

    lbl_countdown_ = makeLabel("05:00", timer_font_px_, 800,
                                theme::V2_TEXT_DARK, Qt::AlignCenter);
    rv->addWidget(lbl_countdown_);

    rv->addSpacing(sc(22));

    // QR box
    qr_box_ = new QFrame;
    qr_box_->setObjectName("qrBox");
    qr_box_->setStyleSheet(
        QString("QFrame#qrBox {background:%1;border:2px solid %2;border-radius:%3px;}")
            .arg(theme::BG_WHITE, theme::V2_QR_BORDER)
            .arg(sc(16)));
    qr_box_->setMinimumSize(sc(420), sc(420));
    qr_box_->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Preferred);

    auto* qr_layout = new QVBoxLayout(qr_box_);
    qr_layout->setContentsMargins(sc(20), sc(20), sc(20), sc(20));
    qr_layout->setSpacing(0);

    qr_widget_ = new QRWidget;
    qr_layout->addWidget(qr_widget_, 1);

    // Overlay (refresh badge) sits above the QR and is hidden until timeout.
    qr_overlay_ = new RefreshBadge(qr_box_);
    qr_overlay_->hide();

    rv->addWidget(qr_box_, 1, Qt::AlignHCenter);

    rv->addSpacing(sc(22));
    auto* hint = makeLabel(
        "Khi hết giờ hệ thống sẽ tự động reset phiên và đóng QR.",
        sc(20), 700, theme::V2_TEXT_MUTED, Qt::AlignCenter);
    hint->setWordWrap(true);
    rv->addWidget(hint);

    body_grid->addWidget(right, 0, 1);

    // ── Countdown timer wiring ────────────────────────────────────────────
    tick_timer_ = new QTimer(this);
    tick_timer_->setInterval(1000);
    connect(tick_timer_, &QTimer::timeout, this, &ScreenExitHourly::onTick);

    setQrState(QrState::Normal);
    resetCountdown(cfg_.ui.payment_timeout_seconds > 0
                       ? cfg_.ui.payment_timeout_seconds
                       : 180);
}

void ScreenExitHourly::setVehicleInfo(const VehicleInfo& vi) {
    current_vi_ = vi;

    if (lbl_plate_)        lbl_plate_->setText(vi.plate.isEmpty() ? "---.--" : vi.plate);
    if (lbl_amount_due_)   lbl_amount_due_->setText(currencyText(vi.amount_due));
    if (lbl_amount_paid_)  lbl_amount_paid_->setText(currencyText(0));
    if (lbl_amount_remain_) lbl_amount_remain_->setText(currencyText(vi.amount_due));

    if (lbl_entry_time_)
        lbl_entry_time_->setText(vi.entry_time.toString("HH:mm:ss"));
    if (lbl_entry_date_)
        lbl_entry_date_->setText(vi.entry_time.toString("dd/MM/yyyy"));
    if (lbl_exit_time_)
        lbl_exit_time_->setText(vi.exit_time.toString("HH:mm:ss"));
    if (lbl_exit_date_)
        lbl_exit_date_->setText(vi.exit_time.toString("dd/MM/yyyy"));

    if (lbl_duration_)
        lbl_duration_->setText(formatDuration(vi.duration_minutes));

    if (qr_widget_ && !vi.qr_url.isEmpty())
        qr_widget_->setUrl(vi.qr_url);

    setQrState(QrState::Normal);
    resetCountdown(cfg_.ui.payment_timeout_seconds > 0
                       ? cfg_.ui.payment_timeout_seconds
                       : 180);
}

void ScreenExitHourly::updatePayment(quint32 received, quint32 remaining) {
    if (lbl_amount_paid_)   lbl_amount_paid_->setText(currencyText(received));
    if (lbl_amount_remain_) lbl_amount_remain_->setText(currencyText(remaining));
}

void ScreenExitHourly::stop() {
    if (tick_timer_) tick_timer_->stop();
}

void ScreenExitHourly::resetCountdown(int seconds) {
    remaining_secs_ = seconds;
    timeout_extra_  = 0;
    updateTimerLabel();
    if (tick_timer_) tick_timer_->start();
}

void ScreenExitHourly::onTick() {
    if (state_ == QrState::TimedOut) {
        ++timeout_extra_;
        if (timeout_extra_ >= kTimedOutHoldSeconds) {
            tick_timer_->stop();
            emit sessionTimedOut();
        }
        return;
    }

    if (remaining_secs_ > 0) --remaining_secs_;
    updateTimerLabel();

    if (remaining_secs_ <= 0) {
        setQrState(QrState::TimedOut);
    } else if (remaining_secs_ <= kWarningSeconds) {
        setQrState(QrState::Warning);
    }
}

void ScreenExitHourly::updateTimerLabel() {
    if (!lbl_countdown_) return;
    const int s = qMax(0, remaining_secs_);
    lbl_countdown_->setText(QString("%1:%2")
        .arg(s / 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0')));
}

void ScreenExitHourly::setQrState(QrState s) {
    state_ = s;

    auto applyText = [&](const QString& msg, const QString& msg_color,
                          const QString& timer_color) {
        if (lbl_message_) {
            lbl_message_->setText(msg);
            lbl_message_->setStyleSheet(
                QString("color:%1;font-size:%2px;font-weight:800;background:transparent;"
                        "font-family:\"Inter\",\"Helvetica Neue\",Arial;")
                .arg(msg_color).arg(msg_font_px_));
        }
        if (lbl_countdown_) {
            lbl_countdown_->setStyleSheet(
                QString("color:%1;font-size:%2px;font-weight:800;background:transparent;"
                        "font-family:\"Inter\",\"Helvetica Neue\",Arial;")
                .arg(timer_color).arg(timer_font_px_));
        }
    };

    // Clear any previous QR effect — Qt only allows one effect per widget.
    if (qr_widget_) qr_widget_->setGraphicsEffect(nullptr);
    if (qr_overlay_) qr_overlay_->hide();

    switch (s) {
        case QrState::Normal:
            applyText("Quý khách có thể bỏ tiền ở ngăn dưới\nhoặc quét QR Code",
                       theme::V2_SUCCESS, theme::V2_TEXT_DARK);
            break;
        case QrState::Warning:
            applyText("Quý khách có thể bỏ tiền ở ngăn dưới\nhoặc quét QR Code",
                       theme::V2_SUCCESS, theme::V2_DANGER);
            break;
        case QrState::TimedOut:
            applyText("Phiên đăng nhập đã hết hạn.\nTự động reset trong 5s.",
                       theme::V2_DANGER, theme::V2_DANGER);
            if (qr_widget_) {
                auto* eff = new QGraphicsOpacityEffect(qr_widget_);
                eff->setOpacity(0.45);
                qr_widget_->setGraphicsEffect(eff);
            }
            if (qr_overlay_ && qr_box_) {
                const int x = (qr_box_->width()  - qr_overlay_->width())  / 2;
                const int y = (qr_box_->height() - qr_overlay_->height()) / 2;
                qr_overlay_->move(qMax(x, 0), qMax(y, 0));
                qr_overlay_->show();
                qr_overlay_->raise();
            }
            break;
    }
}
