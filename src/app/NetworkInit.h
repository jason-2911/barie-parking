/**
 * NetworkInit.h
 * On startup: probe internet connectivity, detect country via IP geolocation,
 * and set the process timezone if the device is in Vietnam.
 *
 * Usage:
 *   auto* ni = new NetworkInit(5000, this);
 *   connect(ni, &NetworkInit::done, this, &MyClass::onNetworkInitDone);
 *   ni->run();
 *
 * The `done` signal fires exactly once — on success, timeout, or error.
 * The caller owns ni and should deleteLater() it inside the done handler.
 */

#pragma once

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class NetworkInit : public QObject
{
    Q_OBJECT

public:
    explicit NetworkInit(int timeout_ms = 5000, QObject* parent = nullptr);

    // Start the async probe. Emits done() exactly once.
    void run();

signals:
    // connected   — false if no network or probe timed out
    // country_code — ISO 3166-1 alpha-2 (e.g. "VN"), empty on failure
    // timezone     — IANA tz string (e.g. "Asia/Ho_Chi_Minh"), empty on failure
    void done(bool connected, const QString& country_code, const QString& timezone);

private:
    void finish(bool connected, const QString& cc, const QString& tz);

    QNetworkAccessManager* nam_      = nullptr;
    QNetworkReply*         reply_    = nullptr;
    QTimer*                timer_    = nullptr;
    bool                   finished_ = false;
    int                    timeout_ms_;
};
