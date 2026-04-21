/**
 * DeviceId.h
 * Reads a stable hardware identifier from the running board so the
 * device does not depend on a hand-edited config value.
 *
 *   Linux : /etc/machine-id → product_uuid → cpuinfo Serial → eth0 MAC
 *   macOS : IOPlatformUUID (ioreg)
 *
 * The returned string is already normalized: lowercase hex, no dashes,
 * no surrounding whitespace. On total failure returns an empty string —
 * callers should fall back to the value provided in config.json.
 */

#pragma once

#include <QString>

namespace DeviceId {

// Blocking best-effort probe. Safe to call once at startup.
// Prefix is prepended to the detected id (e.g. "pk-" → "pk-abcdef…").
QString detect(const QString& prefix = {});

}  // namespace DeviceId
