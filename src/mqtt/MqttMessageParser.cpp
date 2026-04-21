#include "mqtt/MqttMessageParser.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDebug>

InboundMessage MqttMessageParser::parse(const QByteArray& json) {
    InboundMessage out;
    QJsonParseError err;
    auto doc = QJsonDocument::fromJson(json, &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject()) {
        qWarning() << "[MQTT/parse] JSON error:" << err.errorString();
        return out;
    }

    QJsonObject root = doc.object();
    const QString type = root["type"].toString();

    if (type == "vehicle_info") {
        out.type = InboundMsgType::VehicleInfo;
        VehicleInfo vi;
        vi.session_id       = root["session_id"].toString();
        vi.barrier_id       = root["barrier_id"].toString();
        vi.scenario_tag     = root["scenario_tag"].toString();
        vi.direction        = directionFromStr(root["direction"].toString());
        vi.plate            = root["plate"].toString();
        vi.ticket_type      = ticketFromStr(root["ticket_type"].toString());
        vi.entry_time       = QDateTime::fromString(root["entry_time"].toString(), Qt::ISODate);
        vi.exit_time        = QDateTime::fromString(root["exit_time"].toString(), Qt::ISODate);
        vi.duration_minutes = root["duration_minutes"].toInt();
        vi.amount_due       = static_cast<quint32>(root["amount_due"].toInt());
        vi.currency         = root["currency"].toString("VND");
        vi.owner_name       = root["owner_name"].toString();
        vi.valid_until      = QDate::fromString(root["valid_until"].toString(), Qt::ISODate);
        vi.qr_url           = root["qr_url"].toString();
        vi.qr_expires_at    = QDateTime::fromString(root["qr_expires_at"].toString(), Qt::ISODate);
        vi.open             = root["open"].toBool(false);

        for (const auto& m : root["payment_methods"].toArray())
            vi.payment_methods.append(m.toString());

        out.vehicle_info = vi;
    }
    else if (type == "open_cmd") {
        out.type = InboundMsgType::OpenCommand;
        OpenCommand cmd;
        cmd.barrier_id         = root["barrier_id"].toString();
        cmd.session_id         = root["session_id"].toString();
        cmd.play_sound         = root["play_sound"].toBool(true);
        cmd.sound_file         = root["sound_file"].toString();
        cmd.auto_close_seconds = root["auto_close_seconds"].toInt(10);

        const QString r = root["reason"].toString();
        if      (r == "payment_confirmed") cmd.reason = OpenReason::PaymentConfirmed;
        else if (r == "monthly_ticket")    cmd.reason = OpenReason::MonthlyTicket;
        else if (r == "manual_override")   cmd.reason = OpenReason::ManualOverride;
        else if (r == "free_exit")         cmd.reason = OpenReason::FreeExit;

        out.open_cmd = cmd;
    }
    else if (type == "qr_payment_confirmed") {
        out.type = InboundMsgType::QrPaymentConfirmed;
        QrPaymentConfirmed qr;
        qr.session_id     = root["session_id"].toString();
        qr.barrier_id     = root["barrier_id"].toString();
        qr.amount_paid    = static_cast<quint32>(root["amount_paid"].toInt());
        qr.payment_app    = root["payment_app"].toString();
        qr.transaction_id = root["transaction_id"].toString();
        qr.timestamp      = QDateTime::fromString(root["timestamp"].toString(), Qt::ISODate);
        out.qr_confirmed  = qr;
    }
    else if (type == "payment_success") {
        out.type = InboundMsgType::PaymentSuccess;
        PaymentSuccess ps;
        ps.session_id        = root["session_id"].toString();
        // Accept both "barrier_ids" (array) and legacy "barrier_id" (string).
        if (root["barrier_ids"].isArray()) {
            for (const auto& v : root["barrier_ids"].toArray())
                ps.barrier_ids.append(v.toString());
        } else {
            const QString bid = root["barrier_id"].toString();
            if (!bid.isEmpty()) ps.barrier_ids.append(bid);
        }
        ps.plate             = root["plate"].toString();
        ps.status            = root["status"].toString("success");
        ps.auto_idle_seconds = root["auto_idle_seconds"].toInt(0);
        ps.timestamp         = QDateTime::fromString(root["timestamp"].toString(), Qt::ISODate);
        out.payment_success  = ps;
    }
    else if (type == "display_msg") {
        out.type = InboundMsgType::DisplayMessage;
        DisplayMessage dm;
        dm.barrier_id       = root["barrier_id"].toString();
        dm.message          = root["message"].toString();
        dm.duration_seconds = root["duration_seconds"].toInt(10);
        dm.severity         = root["severity"].toString("info");
        dm.blocking         = root.contains("blocking")
                            ? root["blocking"].toBool(true)
                            : true;
        dm.require_ack      = root["require_ack"].toBool(false);
        dm.auto_idle_seconds= root["auto_idle_seconds"].toInt(0);
        dm.play_sound       = root["play_sound"].toBool(false);
        dm.sound_text       = root["sound_text"].toString();
        out.display_msg     = dm;
    }
    else if (type == "reconnect") {
        out.type = InboundMsgType::Reconnect;
        ReconnectCommand rc;
        rc.reason = root["reason"].toString("server_request");
        out.reconnect = rc;
    }

    return out;
}
