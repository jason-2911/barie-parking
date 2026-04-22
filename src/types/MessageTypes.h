/**
 * MessageTypes.h
 * Typed structs for all MQTT messages — both inbound and outbound.
 */

#pragma once

#include <QString>
#include <QDateTime>
#include <QVector>
#include <optional>

// ─── Enums ───────────────────────────────────────────────────────────────────

enum class BarrierDirection { Entry, Exit, Unknown };
enum class TicketType       { Hourly, Monthly, Free, Unknown };
// DeviceType: controls which subsystems the device brings up.
//   - EntryOnly: headless, barrier(s) only, server-driven open/close. NV9 + UI skipped.
//   - ExitOnly:  single exit barrier, NV9 + UI active (current default behaviour).
//   - Full:      entry + exit barriers on the same lane, NV9 + UI active.
enum class DeviceType       { EntryOnly, ExitOnly, Full, Unknown };
enum class PaymentMethod    { Cash, QR, NFC, Unknown };
enum class OpenReason       {
    // Exit-side: triggered by something the user did at the kiosk.
    PaymentConfirmed,   // hourly ticket, cash/QR payment cleared
    MonthlyTicket,      // valid monthly subscription
    FreeExit,           // operator-declared free pass window
    // Entry-side: triggered by the inbound lane / camera / ANPR.
    VehicleEntry,       // any plate detected at the entry lane
    EntryAuthorized,    // pre-paid / whitelisted plate authorized by server
    // Direction-neutral: a guard / dashboard physically forced the gate.
    ManualOverride,
    // Anything we don't recognize — handler must refuse to open.
    Unknown
};

// ─── Device → Server ─────────────────────────────────────────────────────────

struct BarrierInfo {
    QString        id;
    BarrierDirection direction = BarrierDirection::Unknown;
    int            modbus_addr = 1;
    QString        description;
};

struct ConnectMessage {
    QString           device_id;
    QDateTime         timestamp;
    QVector<BarrierInfo> barriers;
    QString           firmware_version;
    bool              has_cash_reader = false;
    bool              has_display     = true;
    bool              has_audio       = true;
};

struct DisconnectMessage {
    QString   device_id;
    QString   reason;       // "graceful" | "lwt" | "barrier_change"
    QDateTime timestamp;
};

// (No application-level ping. Presence is MQTT-native now: PINGREQ frames
// every `keepalive_seconds` keep the TCP session alive, and a retained LWT
// on parking/v1/client/<id>/disconnect fires within ~1.5 × keepalive after
// an ungraceful drop.)

struct PaymentMessage {
    QString       device_id;
    QString       barrier_id;
    QString       session_id;
    QString       plate;
    PaymentMethod method = PaymentMethod::Cash;
    quint32       amount_due    = 0;
    quint32       amount_paid   = 0;
    quint32       amount_change = 0;
    QDateTime     timestamp;
};

// ─── Server → Device ─────────────────────────────────────────────────────────

struct VehicleInfo {
    QString       session_id;
    QString       barrier_id;
    QString       scenario_tag;
    BarrierDirection direction = BarrierDirection::Exit;
    QString       plate;
    TicketType    ticket_type = TicketType::Hourly;

    // Hourly
    QDateTime     entry_time;
    QDateTime     exit_time;
    int           duration_minutes = 0;
    quint32       amount_due       = 0;
    QString       currency         = "VND";

    // Monthly
    QString       owner_name;
    QDate         valid_until;

    // Payment
    QVector<QString> payment_methods;
    QString          qr_url;
    QDateTime        qr_expires_at;
    bool             open = false;   // true = auto-open on arrival (monthly ticket)
};

struct OpenCommand {
    QString     barrier_id;
    QString     session_id;
    OpenReason  reason = OpenReason::Unknown;
    bool        play_sound          = true;
    QString     sound_file;
    int         auto_close_seconds  = 10;
};

struct QrPaymentConfirmed {
    QString   session_id;
    QString   barrier_id;
    quint32   amount_paid   = 0;
    QString   payment_app;
    QString   transaction_id;
    QDateTime timestamp;
};

struct DisplayMessage {
    QString barrier_id;
    QString message;
    int     duration_seconds = 10;
    QString severity = "info";   // info | warning | error | success
    bool    blocking = true;
    bool    require_ack = false;
    int     auto_idle_seconds = 0;
    bool    play_sound = false;
    QString sound_text;
};

// Server → device: payment confirmed. Device switches to the success screen
// and opens every barrier in barrier_ids in order.
// The device already tracks the paid amount locally; the server does not
// need to echo it back. auto_idle_seconds == 0 → use device default.
struct PaymentSuccess {
    QString          session_id;
    QVector<QString> barrier_ids;    // one or more barriers to open
    QString          plate;
    QString          status;         // "success" (reserved for future codes)
    int              auto_idle_seconds = 0;
    QDateTime        timestamp;
};

// Sent by the server (usually on restart) to tell every device to
// re-announce itself via publishConnect(). Allows the server to rebuild
// in-memory state without waiting for the next ping cycle.
struct ReconnectCommand {
    QString reason;          // e.g. "server_restart", "sync"
};

// ─── Union-like inbound command ───────────────────────────────────────────────

enum class InboundMsgType {
    VehicleInfo,
    OpenCommand,
    QrPaymentConfirmed,
    PaymentSuccess,
    DisplayMessage,
    Reconnect,
    Unknown
};

struct InboundMessage {
    InboundMsgType type = InboundMsgType::Unknown;
    std::optional<VehicleInfo>        vehicle_info;
    std::optional<OpenCommand>        open_cmd;
    std::optional<QrPaymentConfirmed> qr_confirmed;
    std::optional<PaymentSuccess>     payment_success;
    std::optional<DisplayMessage>     display_msg;
    std::optional<ReconnectCommand>   reconnect;
};

// ─── Helper converters ────────────────────────────────────────────────────────

inline QString directionStr(BarrierDirection d) {
    switch (d) {
        case BarrierDirection::Entry: return "entry";
        case BarrierDirection::Exit:  return "exit";
        default:                      return "unknown";
    }
}

inline BarrierDirection directionFromStr(const QString& s) {
    if (s == "entry") return BarrierDirection::Entry;
    if (s == "exit")  return BarrierDirection::Exit;
    return BarrierDirection::Unknown;
}

inline QString deviceTypeStr(DeviceType t) {
    switch (t) {
        case DeviceType::EntryOnly: return "entry_only";
        case DeviceType::ExitOnly:  return "exit_only";
        case DeviceType::Full:      return "full";
        default:                    return "unknown";
    }
}

inline DeviceType deviceTypeFromStr(const QString& s) {
    if (s == "entry_only") return DeviceType::EntryOnly;
    if (s == "exit_only")  return DeviceType::ExitOnly;
    if (s == "full")       return DeviceType::Full;
    return DeviceType::Unknown;
}

inline QString openReasonStr(OpenReason r) {
    switch (r) {
        case OpenReason::PaymentConfirmed: return "payment_confirmed";
        case OpenReason::MonthlyTicket:    return "monthly_ticket";
        case OpenReason::FreeExit:         return "free_exit";
        case OpenReason::VehicleEntry:     return "vehicle_entry";
        case OpenReason::EntryAuthorized:  return "entry_authorized";
        case OpenReason::ManualOverride:   return "manual_override";
        default:                           return "unknown";
    }
}

// Direction the reason implies. ManualOverride is direction-neutral —
// returns Unknown on purpose so the caller treats it as "always allowed".
inline BarrierDirection openReasonImpliesDirection(OpenReason r) {
    switch (r) {
        case OpenReason::PaymentConfirmed:
        case OpenReason::MonthlyTicket:
        case OpenReason::FreeExit:
            return BarrierDirection::Exit;
        case OpenReason::VehicleEntry:
        case OpenReason::EntryAuthorized:
            return BarrierDirection::Entry;
        case OpenReason::ManualOverride:
        case OpenReason::Unknown:
        default:
            return BarrierDirection::Unknown;
    }
}

inline TicketType ticketFromStr(const QString& s) {
    if (s == "hourly")  return TicketType::Hourly;
    if (s == "monthly") return TicketType::Monthly;
    if (s == "free")    return TicketType::Free;
    return TicketType::Unknown;
}
