/**
 * MqttTopics.h
 * Single source of truth for MQTT topic names.
 * Must stay in sync with server.py.
 */

#pragma once

#include <QString>

namespace mqtt_topics {

// Device → Server  ("client" segment = device is the publisher)
inline QString connect   (const QString& dev) { return "parking/v1/client/" + dev + "/connect"; }
inline QString disconnect(const QString& dev) { return "parking/v1/client/" + dev + "/disconnect"; }
inline QString payment   (const QString& dev, const QString& barrier_id) {
    return "parking/v1/client/" + dev + "/payment/" + barrier_id;
}
inline QString event     (const QString& dev) { return "parking/v1/client/" + dev + "/event"; }

// Server → Device  ("server" segment = server is the publisher)
inline QString cmd       (const QString& dev) { return "parking/v1/server/" + dev + "/cmd"; }

// Server broadcast to all devices
inline QString broadcast() { return QStringLiteral("parking/v1/broadcast/cmd"); }

} // namespace mqtt_topics
