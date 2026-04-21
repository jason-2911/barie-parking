/**
 * MqttMessageBuilder.h
 * Pure JSON serializers for outbound (device → server) messages.
 * Keeping them stateless makes them trivial to unit-test.
 */

#pragma once

#include <QByteArray>
#include "app/Config.h"
#include "types/MessageTypes.h"

class MqttMessageBuilder {
public:
    static QByteArray buildConnect(const Config& cfg);
    static QByteArray buildDisconnect(const Config& cfg, const QString& reason);
    static QByteArray buildPing(const Config& cfg,
                                 const QVector<PingMessage::BarrierPingInfo>& barriers,
                                 qint64 uptime_seconds,
                                 int cpu_temp_c,
                                 int free_mem_mb);
    static QByteArray buildPayment(const PaymentMessage& msg);

    // Emitted when the user walked away / didn't finish payment in time.
    // Lets the server release the session so it isn't held open forever.
    static QByteArray buildSessionAbandoned(const Config& cfg,
                                              const QString& session_id,
                                              const QString& barrier_id,
                                              const QString& plate,
                                              quint32 amount_due,
                                              quint32 amount_paid);

    // User pressed the on-screen support button. Server side should route
    // this to the security / ops dashboard so a guard can respond.
    static QByteArray buildSupportRequest(const Config& cfg,
                                            const QString& session_id,
                                            const QString& barrier_id,
                                            const QString& plate,
                                            const QString& reason);
};
