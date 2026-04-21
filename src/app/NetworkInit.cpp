/**
 * NetworkInit.cpp — See NetworkInit.h
 */

#include "app/NetworkInit.h"

#include <QDebug>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QProcess>

#include <ctime>   // tzset()

NetworkInit::NetworkInit(int timeout_ms, QObject* parent)
    : QObject(parent), timeout_ms_(timeout_ms)
{
    nam_   = new QNetworkAccessManager(this);
    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
}

void NetworkInit::run()
{
    // Abort and emit done() if the probe takes too long.
    connect(timer_, &QTimer::timeout, this, [this]() {
        qWarning() << "[NetworkInit] timeout after" << timeout_ms_ << "ms"
                   << "— proceeding without geolocation";
        if (reply_) { reply_->abort(); reply_ = nullptr; }
        finish(false, {}, {});
    });

    // ip-api.com free tier — no auth, returns JSON with countryCode + timezone.
    // Fields are cherry-picked to keep the payload small.
    QNetworkRequest req{QUrl("http://ip-api.com/json?fields=status,countryCode,timezone")};
    req.setHeader(QNetworkRequest::UserAgentHeader, "SmartParkKiosk/1.0");
    req.setTransferTimeout(timeout_ms_);

    reply_ = nam_->get(req);
    connect(reply_, &QNetworkReply::finished, this, [this]() {
        if (finished_) return;

        const auto err = reply_->error();
        const auto body = reply_->readAll();
        reply_->deleteLater();
        reply_ = nullptr;

        if (err != QNetworkReply::NoError) {
            qWarning() << "[NetworkInit] HTTP error:" << err;
            finish(false, {}, {});
            return;
        }

        QJsonParseError je;
        const auto doc = QJsonDocument::fromJson(body, &je);
        if (je.error != QJsonParseError::NoError || !doc.isObject()) {
            qWarning() << "[NetworkInit] bad JSON:" << je.errorString();
            finish(false, {}, {});
            return;
        }

        const auto obj      = doc.object();
        const QString status = obj["status"].toString();
        const QString cc     = obj["countryCode"].toString();
        const QString tz     = obj["timezone"].toString();

        qInfo() << "[NetworkInit] status=" << status
                << "country=" << cc << "timezone=" << tz;

        finish(status == "success", cc, tz);
    });

    timer_->start(timeout_ms_);
}

void NetworkInit::finish(bool connected, const QString& cc, const QString& tz)
{
    if (finished_) return;
    finished_ = true;
    timer_->stop();

    if (connected && cc == "VN") {
        const QString apply_tz = tz.isEmpty() ? "Asia/Ho_Chi_Minh" : tz;
        qInfo() << "[NetworkInit] Vietnam detected — applying timezone:" << apply_tz;

        // Set for this process and all child processes.
        qputenv("TZ", apply_tz.toUtf8());
        ::tzset();

        // Best-effort system-level set (only works if running as root or with
        // the appropriate polkit rule; failure is non-fatal).
        QProcess::startDetached("timedatectl", {"set-timezone", apply_tz});
    }

    emit done(connected, cc, tz);
}
