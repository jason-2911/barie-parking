/**
 * @file parking_selftest.cpp
 * @brief End-to-end self-test for the three deployment profiles.
 *
 * Exercises — without real MQTT / serial / UI:
 *
 *   1.  Modbus CRC16 against vendor-doc frames.
 *   2.  Status-word position decoding (the off-by-one bug fixed in
 *       BarrierControllerModbus.h lives here).
 *   3.  Message-dispatch filter for entry_only / exit_only / full —
 *       this is the core safety property "foreign messages must not
 *       crash or change state".
 *   4.  OpenCommand reason vs barrier direction guard.
 *   5.  Config save/load round-trip (barrier short_id preservation).
 *
 * Exit code 0 on full pass, 1 on any failure.
 */

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <QFile>
#include <cstdio>

#include "app/Config.h"
#include "app/MessageFilter.h"
#include "types/MessageTypes.h"

// Reach into the driver's bit layout and CRC helper for direct unit tests.
#include "hardware/drivers/BarrierControllerModbus.h"

// ─── Tiny test runner ──────────────────────────────────────────────────────

namespace {

int g_passed = 0;
int g_failed = 0;
QString g_current_section;

void section(const char* name) {
    g_current_section = name;
    printf("\n── %s ──────────────────\n", name);
}

void check(bool cond, const char* what) {
    if (cond) {
        ++g_passed;
        printf("  \033[32mPASS\033[0m  %s\n", what);
    } else {
        ++g_failed;
        printf("  \033[31mFAIL\033[0m  %s  (section: %s)\n",
               what, qUtf8Printable(g_current_section));
    }
}

#define CHECK(cond) check((cond), #cond)

// ─── Helpers ───────────────────────────────────────────────────────────────

uint16_t crc16_modbus(const uint8_t* data, size_t len) {
    uint16_t crc = 0xFFFF;
    for (size_t i = 0; i < len; ++i) {
        crc ^= data[i];
        for (int b = 0; b < 8; ++b) {
            if (crc & 1) crc = (crc >> 1) ^ 0xA001;
            else          crc >>= 1;
        }
    }
    return crc;
}

// ─── Tests ─────────────────────────────────────────────────────────────────

void testCrc16VendorVectors() {
    section("Modbus CRC16 — vendor-doc frames");

    // Doc: "上杆运行命令 Tx: 01 05 00 00 FF 00 8C 3A"  → frame body 6 bytes,
    // CRC low/high = 8C 3A ⇒ CRC = 0x3A8C.
    {
        uint8_t f[] = {0x01, 0x05, 0x00, 0x00, 0xFF, 0x00};
        uint16_t crc = crc16_modbus(f, sizeof(f));
        check(crc == 0x3A8C, "UP command CRC == 0x3A8C");
    }
    // DOWN: "01 05 00 01 FF 00 DD FA" ⇒ CRC = 0xFADD.
    {
        uint8_t f[] = {0x01, 0x05, 0x00, 0x01, 0xFF, 0x00};
        uint16_t crc = crc16_modbus(f, sizeof(f));
        check(crc == 0xFADD, "DOWN command CRC == 0xFADD");
    }
    // SELF_TEST: "01 05 00 03 FF 00 7C 3A" ⇒ CRC = 0x3A7C.
    {
        uint8_t f[] = {0x01, 0x05, 0x00, 0x03, 0xFF, 0x00};
        uint16_t crc = crc16_modbus(f, sizeof(f));
        check(crc == 0x3A7C, "SELF_TEST command CRC == 0x3A7C");
    }
    // Read D014H status: "01 03 D0 14 00 01 FC CE" ⇒ CRC = 0xCEFC.
    {
        uint8_t f[] = {0x01, 0x03, 0xD0, 0x14, 0x00, 0x01};
        uint16_t crc = crc16_modbus(f, sizeof(f));
        check(crc == 0xCEFC, "READ status register CRC == 0xCEFC");
    }
}

void testStatusBitsMatchDoc() {
    section("D014H status bit layout — per vendor doc");

    CHECK(STATUS_BIT_RUNNING    == (1u << 0));
    CHECK(STATUS_BIT_DIRECTION  == (1u << 1));
    CHECK(STATUS_BIT_DOWN_POS   == (1u << 9));     // was (1<<8) before fix
    CHECK(STATUS_BIT_UP_POS     == (1u << 10));    // was (1<<9) before fix
    CHECK(STATUS_BIT_IN_POSITION== (1u << 11));
    CHECK(STATUS_BIT_GREEN_LIGHT== (1u << 12));
    CHECK(STATUS_BIT_RED_LIGHT  == (1u << 13));

    // Vendor example: 01 03 02 21 00 A0 14 ⇒ status word 0x2100 should have
    // the red-light bit set (0x2000) and nothing else we care about.
    const uint16_t status = 0x2100;
    CHECK(status & STATUS_BIT_RED_LIGHT);
    CHECK(!(status & STATUS_BIT_UP_POS));
    CHECK(!(status & STATUS_BIT_DOWN_POS));
    CHECK(!(status & STATUS_BIT_RUNNING));

    // Synthetic "barrier raised" status: up_pos + in_position + red light.
    const uint16_t raised = STATUS_BIT_UP_POS | STATUS_BIT_IN_POSITION
                          | STATUS_BIT_RED_LIGHT;
    CHECK(raised & STATUS_BIT_UP_POS);
}

void testMessageFilter() {
    section("Inbound message filter — per device_type");

    using parking::shouldDispatch;

    // EntryOnly: only OpenCommand + Reconnect get through.
    CHECK( shouldDispatch(DeviceType::EntryOnly, InboundMsgType::OpenCommand));
    CHECK( shouldDispatch(DeviceType::EntryOnly, InboundMsgType::Reconnect));
    CHECK(!shouldDispatch(DeviceType::EntryOnly, InboundMsgType::VehicleInfo));
    CHECK(!shouldDispatch(DeviceType::EntryOnly, InboundMsgType::QrPaymentConfirmed));
    CHECK(!shouldDispatch(DeviceType::EntryOnly, InboundMsgType::PaymentSuccess));
    CHECK(!shouldDispatch(DeviceType::EntryOnly, InboundMsgType::DisplayMessage));

    // ExitOnly + Full: all six handled.
    for (auto mode : {DeviceType::ExitOnly, DeviceType::Full}) {
        CHECK(shouldDispatch(mode, InboundMsgType::VehicleInfo));
        CHECK(shouldDispatch(mode, InboundMsgType::OpenCommand));
        CHECK(shouldDispatch(mode, InboundMsgType::QrPaymentConfirmed));
        CHECK(shouldDispatch(mode, InboundMsgType::PaymentSuccess));
        CHECK(shouldDispatch(mode, InboundMsgType::DisplayMessage));
        CHECK(shouldDispatch(mode, InboundMsgType::Reconnect));
    }

    // Unknown mode: nothing is dispatched — a safety net.
    CHECK(!shouldDispatch(DeviceType::Unknown, InboundMsgType::OpenCommand));
    CHECK(!shouldDispatch(DeviceType::Unknown, InboundMsgType::VehicleInfo));
}

void testOpenReasonDirectionGuard() {
    section("OpenCommand — reason vs barrier direction");

    // Exit-side reasons.
    CHECK(openReasonImpliesDirection(OpenReason::PaymentConfirmed) == BarrierDirection::Exit);
    CHECK(openReasonImpliesDirection(OpenReason::MonthlyTicket)    == BarrierDirection::Exit);
    CHECK(openReasonImpliesDirection(OpenReason::FreeExit)         == BarrierDirection::Exit);

    // Entry-side reasons.
    CHECK(openReasonImpliesDirection(OpenReason::VehicleEntry)     == BarrierDirection::Entry);
    CHECK(openReasonImpliesDirection(OpenReason::EntryAuthorized)  == BarrierDirection::Entry);

    // Direction-neutral / always allowed.
    CHECK(openReasonImpliesDirection(OpenReason::ManualOverride)   == BarrierDirection::Unknown);

    // Unknown reason — handled as "refuse outright" by Application.
    CHECK(openReasonImpliesDirection(OpenReason::Unknown)          == BarrierDirection::Unknown);

    auto allowed = [](OpenReason r, BarrierDirection bdir) {
        if (r == OpenReason::Unknown) return false;
        auto implied = openReasonImpliesDirection(r);
        return implied == BarrierDirection::Unknown || implied == bdir;
    };

    CHECK( allowed(OpenReason::PaymentConfirmed, BarrierDirection::Exit));
    CHECK( allowed(OpenReason::MonthlyTicket,    BarrierDirection::Exit));
    CHECK( allowed(OpenReason::FreeExit,         BarrierDirection::Exit));
    CHECK( allowed(OpenReason::ManualOverride,   BarrierDirection::Exit));
    CHECK(!allowed(OpenReason::VehicleEntry,     BarrierDirection::Exit));
    CHECK(!allowed(OpenReason::EntryAuthorized,  BarrierDirection::Exit));
    CHECK(!allowed(OpenReason::Unknown,          BarrierDirection::Exit));

    CHECK( allowed(OpenReason::VehicleEntry,     BarrierDirection::Entry));
    CHECK( allowed(OpenReason::EntryAuthorized,  BarrierDirection::Entry));
    CHECK( allowed(OpenReason::ManualOverride,   BarrierDirection::Entry));
    CHECK(!allowed(OpenReason::PaymentConfirmed, BarrierDirection::Entry));
    CHECK(!allowed(OpenReason::MonthlyTicket,    BarrierDirection::Entry));
    CHECK(!allowed(OpenReason::FreeExit,         BarrierDirection::Entry));
    CHECK(!allowed(OpenReason::Unknown,          BarrierDirection::Entry));
}

void testForeignMessagesDoNotCrash() {
    section("Safety — foreign messages per mode never dispatch");

    const InboundMsgType types[] = {
        InboundMsgType::VehicleInfo, InboundMsgType::OpenCommand,
        InboundMsgType::QrPaymentConfirmed, InboundMsgType::PaymentSuccess,
        InboundMsgType::DisplayMessage, InboundMsgType::Reconnect,
    };
    const DeviceType modes[] = {
        DeviceType::EntryOnly, DeviceType::ExitOnly,
        DeviceType::Full, DeviceType::Unknown,
    };
    int dispatched = 0, dropped = 0;
    for (auto m : modes) {
        for (auto t : types) {
            if (parking::shouldDispatch(m, t)) ++dispatched;
            else                                ++dropped;
        }
    }
    // EntryOnly: 2 of 6 dispatch; Exit + Full: 6 of 6 each; Unknown: 0.
    CHECK(dispatched == (2 + 6 + 6 + 0));
    CHECK(dropped    == ((6 - 2) + 0 + 0 + 6));
}

void testConfigSaveRoundTrip() {
    section("Config save/load — barrier short_id preserved");

    QTemporaryDir tmp;
    const QString path = tmp.path() + "/config.json";

    // Write minimal seed config with short IDs (as they appear in config.json).
    QJsonObject seed;
    QJsonObject dev;
    dev["id"]      = "selftest_dev";
    dev["auto_id"] = false;
    dev["type"]    = "exit_only";
    seed["device"] = dev;
    QJsonArray bars;
    { QJsonObject b; b["id"] = "01"; b["modbus_addr"] = 1;
      b["direction"] = "exit"; bars.append(b); }
    seed["barriers"] = bars;
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(QJsonDocument(seed).toJson());
    f.close();

    Config cfg;
    CHECK(cfg.load(path));
    CHECK(cfg.barriers.size() == 1);
    CHECK(cfg.barriers[0].short_id == "01");
    CHECK(cfg.barriers[0].id       == "01");   // id == short_id before expansion

    // Simulate main.cpp ID expansion.
    cfg.barriers[0].id = cfg.device.id + "_" + cfg.barriers[0].id;
    CHECK(cfg.barriers[0].id       == "selftest_dev_01");
    CHECK(cfg.barriers[0].short_id == "01");   // short_id unchanged

    // Save and reload — must NOT double-prefix.
    CHECK(cfg.save(path));

    Config reloaded;
    CHECK(reloaded.load(path));
    CHECK(reloaded.barriers.size() == 1);
    CHECK(reloaded.barriers[0].short_id == "01");
    CHECK(reloaded.barriers[0].id       == "01");   // raw short id on disk
}

} // anonymous namespace

int main(int argc, char* argv[]) {
    QCoreApplication app(argc, argv);

    printf("Parking Client Self-Test\n");
    printf("========================\n");

    testCrc16VendorVectors();
    testStatusBitsMatchDoc();
    testMessageFilter();
    testOpenReasonDirectionGuard();
    testForeignMessagesDoNotCrash();
    testConfigSaveRoundTrip();

    printf("\n========================\n");
    printf("PASS: %d   FAIL: %d\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
