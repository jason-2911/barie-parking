#include "ui/MainWindow.h"
#include "ui/Theme.h"
#include "ui/screens/ScreenEntryIdle.h"
#include "ui/screens/ScreenEntryOpen.h"
#include "ui/screens/ScreenExitHourly.h"
#include "ui/screens/ScreenExitMonthly.h"
#include "ui/screens/ScreenDisplayMessage.h"
#include "ui/screens/ScreenPaymentSuccess.h"
#include "ui/screens/ScreenPaymentFailed.h"
#include "ui/widgets/TopBar.h"
#include "ui/widgets/Footer.h"

#include <QVBoxLayout>
#include <QLabel>
#include <QTimer>
#include <QApplication>
#include <QFont>

MainWindow::MainWindow(const Config& cfg, QWidget* parent)
    : QMainWindow(parent), cfg_(cfg)
{
    setWindowTitle("SmartPark");
    setFixedSize(cfg_.ui.width, cfg_.ui.height);

    QFont appFont;
    appFont.setFamilies({"Helvetica Neue", "Arial", "Noto Sans", "SF Pro Display"});
    appFont.setPixelSize(14);
    appFont.setStyleHint(QFont::SansSerif);
    qApp->setFont(appFont);

    setStyleSheet(theme::globalStylesheet());
    buildLayout();
    setupNotification();
}

void MainWindow::buildLayout() {
    central_ = new QWidget(this);
    setCentralWidget(central_);

    vbox_ = new QVBoxLayout(central_);
    vbox_->setContentsMargins(0, 0, 0, 0);
    vbox_->setSpacing(0);

    topbar_ = new TopBar(cfg_, central_);
    vbox_->addWidget(topbar_);

    stack_ = new QStackedWidget(central_);
    stack_->setStyleSheet("background:transparent;border:none;");
    vbox_->addWidget(stack_, 1);

    footer_ = new Footer(central_);
    vbox_->addWidget(footer_);

    screen_idle_        = new ScreenEntryIdle(cfg_);
    screen_entry_open_  = new ScreenEntryOpen(cfg_);
    screen_exit_h_      = new ScreenExitHourly(cfg_);
    screen_exit_m_      = new ScreenExitMonthly(cfg_);
    screen_success_     = new ScreenPaymentSuccess;
    screen_failed_      = new ScreenPaymentFailed;
    screen_display_     = new ScreenDisplayMessage;

    stack_->addWidget(screen_idle_);        // 0
    stack_->addWidget(screen_entry_open_);  // 1
    stack_->addWidget(screen_exit_h_);      // 2
    stack_->addWidget(screen_exit_m_);      // 3
    stack_->addWidget(screen_success_);     // 4
    stack_->addWidget(screen_failed_);      // 5
    stack_->addWidget(screen_display_);     // 6

    connect(screen_idle_,    &ScreenEntryIdle::callSupportRequested,
            this, &MainWindow::callSupportRequested);
    connect(screen_exit_h_,  &ScreenExitHourly::callSupportRequested,
            this, &MainWindow::callSupportRequested);
    connect(screen_exit_h_,  &ScreenExitHourly::sessionTimedOut,
            this, &MainWindow::showIdle);
    connect(screen_exit_m_,  &ScreenExitMonthly::callSupportRequested,
            this, &MainWindow::callSupportRequested);
    connect(screen_success_, &ScreenPaymentSuccess::countdownExpired,
            this, &MainWindow::showIdle);
    connect(screen_failed_,  &ScreenPaymentFailed::countdownExpired,
            this, &MainWindow::showIdle);
    connect(screen_display_, &ScreenDisplayMessage::dismissed,
            this, &MainWindow::restoreAfterDisplayMessage);
}

void MainWindow::setupNotification() {
    notif_label_ = new QLabel(this);
    notif_label_->setAlignment(Qt::AlignCenter);
    notif_label_->setWordWrap(true);
    notif_label_->setStyleSheet(
        "QLabel {"
        "  background:rgba(39,39,42,0.94);"
        "  color:#ffffff;"
        "  font-size:18px;"
        "  font-weight:600;"
        "  border:1px solid rgba(249,115,22,0.5);"
        "  border-radius:16px;"
        "  padding:20px 36px;"
        "}");
    notif_label_->hide();

    notif_timer_ = new QTimer(this);
    notif_timer_->setSingleShot(true);
    connect(notif_timer_, &QTimer::timeout, notif_label_, &QLabel::hide);
}

// ── Screen transitions ────────────────────────────────────────────────────────

void MainWindow::showIdle() {
    // Cancel any running per-screen timers so they cannot fire late and
    // overwrite a fresh customer session.
    if (current_screen_ == Screen::Success) screen_success_->stopCountdown();
    if (current_screen_ == Screen::Failed)  screen_failed_->stopCountdown();
    if (current_screen_ == Screen::ExitHourly) screen_exit_h_->stop();
    if (current_screen_ == Screen::DisplayMessage) screen_display_->stop();
    current_screen_ = Screen::Idle;
    stack_->setCurrentWidget(screen_idle_);
    topbar_->setDirection(cfg_.barriers.isEmpty()
                          ? BarrierDirection::Unknown
                          : cfg_.barriers[0].direction);
}

void MainWindow::showBarrierOpening(const QString& barrier_id,
                                     const QString& plate)
{
    Q_UNUSED(barrier_id)
    if (current_screen_ == Screen::Success) screen_success_->stopCountdown();
    if (current_screen_ == Screen::Failed)  screen_failed_->stopCountdown();
    if (current_screen_ == Screen::ExitHourly) screen_exit_h_->stop();
    if (current_screen_ == Screen::DisplayMessage) screen_display_->stop();
    current_screen_ = Screen::EntryOpen;
    screen_entry_open_->refresh();
    screen_entry_open_->setPlate(plate);
    stack_->setCurrentWidget(screen_entry_open_);
}

void MainWindow::showExitHourly(const VehicleInfo& vi) {
    // New customer arrived — immediately preempt any running countdown.
    if (current_screen_ == Screen::Success) screen_success_->stopCountdown();
    if (current_screen_ == Screen::Failed)  screen_failed_->stopCountdown();
    if (current_screen_ == Screen::DisplayMessage) screen_display_->stop();
    current_screen_ = Screen::ExitHourly;
    screen_exit_h_->setVehicleInfo(vi);
    stack_->setCurrentWidget(screen_exit_h_);
}

void MainWindow::showExitMonthly(const VehicleInfo& vi) {
    if (current_screen_ == Screen::Success) screen_success_->stopCountdown();
    if (current_screen_ == Screen::Failed)  screen_failed_->stopCountdown();
    if (current_screen_ == Screen::ExitHourly) screen_exit_h_->stop();
    if (current_screen_ == Screen::DisplayMessage) screen_display_->stop();
    current_screen_ = Screen::ExitMonthly;
    screen_exit_m_->setVehicleInfo(vi);
    stack_->setCurrentWidget(screen_exit_m_);
}

void MainWindow::showPaymentSuccess(const QString& plate,
                                     quint32         amount,
                                     const QString& method,
                                     int             countdown_seconds)
{
    if (current_screen_ == Screen::ExitHourly) screen_exit_h_->stop();
    if (current_screen_ == Screen::Failed)     screen_failed_->stopCountdown();
    current_screen_ = Screen::Success;
    screen_success_->setResult(plate, amount, method);
    screen_success_->startCountdown(countdown_seconds > 0 ? countdown_seconds : 5);
    stack_->setCurrentWidget(screen_success_);
}

void MainWindow::showPaymentFailed(int countdown_seconds) {
    if (current_screen_ == Screen::ExitHourly) screen_exit_h_->stop();
    if (current_screen_ == Screen::Success)    screen_success_->stopCountdown();
    current_screen_ = Screen::Failed;
    screen_failed_->startCountdown(countdown_seconds > 0 ? countdown_seconds : 5);
    stack_->setCurrentWidget(screen_failed_);
}

void MainWindow::showDisplayMessage(const DisplayMessage& dm) {
    if (!dm.blocking) {
        showNotification(dm.message, dm.duration_seconds * 1000);
        return;
    }

    if (current_screen_ == Screen::Success)
        screen_success_->stopCountdown();
    if (current_screen_ == Screen::Failed)
        screen_failed_->stopCountdown();
    if (current_screen_ == Screen::ExitHourly)
        screen_exit_h_->stop();
    if (current_screen_ == Screen::DisplayMessage)
        screen_display_->stop();

    previous_screen_before_display_ = current_screen_;
    current_screen_ = Screen::DisplayMessage;
    screen_display_->setMessage(dm);
    stack_->setCurrentWidget(screen_display_);
}

// ── Status updates ────────────────────────────────────────────────────────────

void MainWindow::updatePaymentReceived(quint32 received, quint32 remaining) {
    if (current_screen_ == Screen::ExitHourly)
        screen_exit_h_->updatePayment(received, remaining);
}

void MainWindow::showNotification(const QString& msg, int duration_ms) {
    notif_label_->setText(msg);
    const int w = qMin(720, width() - 80);
    notif_label_->setFixedWidth(w);
    // Let the label grow vertically to fit multi-line content rather
    // than clipping at a hardcoded height.
    notif_label_->adjustSize();
    const int h = notif_label_->sizeHint().height();
    notif_label_->resize(w, h);
    notif_label_->move((width() - w) / 2, (height() - h) / 2);
    notif_label_->show();
    notif_label_->raise();
    notif_timer_->start(duration_ms);
}

void MainWindow::restoreAfterDisplayMessage() {
    if (current_screen_ != Screen::DisplayMessage)
        return;

    const Screen target = previous_screen_before_display_;
    current_screen_ = target;
    switch (target) {
        case Screen::Idle:
            stack_->setCurrentWidget(screen_idle_);
            break;
        case Screen::EntryOpen:
            stack_->setCurrentWidget(screen_entry_open_);
            break;
        case Screen::ExitHourly:
            stack_->setCurrentWidget(screen_exit_h_);
            break;
        case Screen::ExitMonthly:
            stack_->setCurrentWidget(screen_exit_m_);
            break;
        case Screen::Success:
        case Screen::Failed:
        case Screen::DisplayMessage:
            stack_->setCurrentWidget(screen_idle_);
            current_screen_ = Screen::Idle;
            break;
    }
}
