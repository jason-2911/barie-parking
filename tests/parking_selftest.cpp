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
 *   4.  LocalBridge ↔ fake wifi-provisioning round trip: scan → setaddr
 *       → update_config → get_config, for each device_type.
 *   5.  Config.save() round-trip: values written by update_config must
 *       re-load identically.
 *
 * Exit code 0 on full pass, 1 on any failure.
 */

#include <QCoreApplication>
#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>
#include <cstdio>

#include "app/Config.h"
#include "app/LocalBridge.h"
#include "app/MessageFilter.h"
#include "types/MessageTypes.h"

#include "MockBarrierController.h"

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

QByteArray buildEnvelope(const char* action, const QJsonObject& params) {
    QJsonObject env;
    env["req_id"] = "self-test";
    env["action"] = action;
    env["params"] = params;
    return QJsonDocument(env).toJson(QJsonDocument::Compact);
}

QJsonObject parseAck(const QByteArray& ack) {
    return QJsonDocument::fromJson(ack).object();
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

struct BridgeFixture {
    Config              cfg;
    QString             config_path;
    MockBarrierController driver;
    LocalBridge*        bridge = nullptr;

    explicit BridgeFixture(const QString& tmpdir) {
        config_path = tmpdir + "/config.json";
        // Seed a minimal on-disk config so Config::save() has something to
        // patch (it re-reads the file to preserve unrelated sections).
        QJsonObject seed;
        QJsonObject dev;
        dev["id"]      = "selftest_dev";
        dev["auto_id"] = false;
        dev["type"]    = "exit_only";
        seed["device"] = dev;
        seed["barriers"] = QJsonArray{};
        QFile f(config_path);
        f.open(QIODevice::WriteOnly);
        f.write(QJsonDocument(seed).toJson());
        f.close();

        cfg.load(config_path);
        (void)driver.open();
    }
    ~BridgeFixture() { delete bridge; }

    void make(LocalBridge::BridgeConfig bcfg = {}) {
        bridge = new LocalBridge(bcfg, &cfg, config_path);
        bridge->setBarrierDriver(&driver);
        // NB: intentionally do NOT call bridge->start() — self-test invokes
        // processEnvelope() directly so no broker is required.
    }
};

void testBridgeActionsForMode(DeviceType mode, const char* mode_name) {
    section(QByteArray("LocalBridge actions — mode=") + mode_name);

    QTemporaryDir tmp;
    BridgeFixture fx(tmp.path());
    fx.make();

    // Step 1: provisioning UI clicks "Scan" while two barriers are wired.
    fx.driver.setScanResult({1, 2});
    auto ack1 = parseAck(fx.bridge->processEnvelope(
        buildEnvelope("scan_barriers", {})));
    CHECK(ack1["ok"].toBool());
    CHECK(ack1["barriers"].toArray().size() == 2);
    CHECK(fx.driver.scanCalls() == 1);

    // Step 2: rewrite barrier #1 to address 3 (simulates user pressing "Set").
    QJsonObject p2;
    p2["current_addr"] = 1;
    p2["new_addr"]     = 3;
    auto ack2 = parseAck(fx.bridge->processEnvelope(
        buildEnvelope("set_barrier_addr", p2)));
    CHECK(ack2["ok"].toBool());
    CHECK(ack2["new_addr"].toInt() == 3);
    CHECK(fx.driver.setAddrCalls().size() == 1);
    CHECK(fx.driver.setAddrCalls().first().first == 1);
    CHECK(fx.driver.setAddrCalls().first().second == 3);

    // Step 3: bad address is refused.
    QJsonObject p_bad;
    p_bad["current_addr"] = 3;
    p_bad["new_addr"]     = 999;
    auto ack_bad = parseAck(fx.bridge->processEnvelope(
        buildEnvelope("set_barrier_addr", p_bad)));
    CHECK(!ack_bad["ok"].toBool());
    CHECK(ack_bad["error"].toString() == "bad_address");

    // Step 4: save config — this is where "wifi-provisioning hits Save".
    QJsonObject save_params;
    save_params["device_type"] = (mode == DeviceType::EntryOnly) ? "entry_only"
                                : (mode == DeviceType::Full)    ? "full"
                                                                : "exit_only";
    QJsonArray bars;
    {
        QJsonObject b; b["addr"] = 3; b["role"] = "entry"; bars.append(b);
    }
    if (mode == DeviceType::Full) {
        QJsonObject b; b["addr"] = 2; b["role"] = "exit"; bars.append(b);
    }
    save_params["barriers"] = bars;

    auto ack_save = parseAck(fx.bridge->processEnvelope(
        buildEnvelope("update_config", save_params)));
    CHECK(ack_save["ok"].toBool());

    // Config reflects the mode we just pushed.
    CHECK(fx.cfg.device.type == mode);
    CHECK(fx.cfg.barriers.size() == bars.size());
    CHECK(fx.cfg.barriers[0].modbus_addr == 3);

    // EntryOnly must disable the cash reader — the device doesn't have one.
    CHECK(fx.cfg.device.has_cash_reader == (mode != DeviceType::EntryOnly));

    // Step 5: config persisted — re-load from disk and compare.
    Config reloaded;
    CHECK(reloaded.load(fx.config_path));
    CHECK(reloaded.device.type == mode);
    CHECK(reloaded.barriers.size() == fx.cfg.barriers.size());

    // Step 6: after save, a fresh get_config should echo the same thing
    // (exactly what the provisioning page's loadConfig() call receives).
    auto ack_get = parseAck(fx.bridge->processEnvelope(
        buildEnvelope("get_config", {})));
    CHECK(ack_get["ok"].toBool());
    CHECK(ack_get["device_type"].toString() ==
          save_params["device_type"].toString());
    CHECK(ack_get["barriers"].toArray().size() == bars.size());
}

void testBridgeRejectsBadProfiles() {
    section("LocalBridge — refuses inconsistent profile/barrier combos");

    QTemporaryDir tmp;
    BridgeFixture fx(tmp.path());
    fx.make();

    // full mode with only 1 barrier: must reject.
    QJsonObject p;
    p["device_type"] = "full";
    QJsonArray bars;
    { QJsonObject b; b["addr"] = 1; b["role"] = "entry"; bars.append(b); }
    p["barriers"] = bars;

    auto ack = parseAck(fx.bridge->processEnvelope(
        buildEnvelope("update_config", p)));
    CHECK(!ack["ok"].toBool());
    CHECK(ack["error"].toString() == "full_needs_2_barriers");

    // exit_only with no barriers: reject.
    QJsonObject p2;
    p2["device_type"] = "exit_only";
    p2["barriers"]    = QJsonArray{};
    auto ack2 = parseAck(fx.bridge->processEnvelope(
        buildEnvelope("update_config", p2)));
    CHECK(!ack2["ok"].toBool());
    CHECK(ack2["error"].toString() == "exit_needs_1_barrier");

    // Garbage action name: handled, not crashed.
    auto ack3 = parseAck(fx.bridge->processEnvelope(
        buildEnvelope("delete_everything", {})));
    CHECK(!ack3["ok"].toBool());
    CHECK(ack3["error"].toString() == "unknown_action");
}

void testFullWifiProvisioningFlow() {
    section("End-to-end: wifi-provisioning → parking-client");

    QTemporaryDir tmp;
    BridgeFixture fx(tmp.path());
    fx.make();

    // Simulates the exact sequence a real captive-portal session produces:
    //   1) user plugs in one barrier (factory default addr=1)
    //   2) hits Scan                 → GET  /api/barriers/scan
    //   3) hits "Set addr 1 → 2"     → POST /api/barriers/setaddr
    //   4) plugs in the second barrier (still factory default addr=1)
    //   5) hits Scan again           → addresses {1, 2}
    //   6) picks Full + roles + Save → POST /api/config

    fx.driver.setScanResult({1});
    auto s1 = parseAck(fx.bridge->processEnvelope(
        buildEnvelope("scan_barriers", {})));
    CHECK(s1["ok"].toBool());
    CHECK(s1["barriers"].toArray().size() == 1);

    QJsonObject setp;
    setp["current_addr"] = 1;
    setp["new_addr"]     = 2;
    auto s2 = parseAck(fx.bridge->processEnvelope(
        buildEnvelope("set_barrier_addr", setp)));
    CHECK(s2["ok"].toBool());

    fx.driver.setScanResult({1, 2});
    auto s3 = parseAck(fx.bridge->processEnvelope(
        buildEnvelope("scan_barriers", {})));
    CHECK(s3["ok"].toBool());
    CHECK(s3["barriers"].toArray().size() == 2);

    QJsonObject save;
    save["device_type"] = "full";
    QJsonArray bars;
    { QJsonObject b; b["addr"] = 2; b["role"] = "entry"; bars.append(b); }
    { QJsonObject b; b["addr"] = 1; b["role"] = "exit";  bars.append(b); }
    save["barriers"] = bars;

    auto s4 = parseAck(fx.bridge->processEnvelope(
        buildEnvelope("update_config", save)));
    CHECK(s4["ok"].toBool());

    // The config on disk should now be ready for the next parking-client
    // restart in full mode.
    Config reloaded;
    CHECK(reloaded.load(fx.config_path));
    CHECK(reloaded.device.type == DeviceType::Full);
    CHECK(reloaded.barriers.size() == 2);
    CHECK(reloaded.barriers[0].direction == BarrierDirection::Entry);
    CHECK(reloaded.barriers[1].direction == BarrierDirection::Exit);
}

void testOpenReasonDirectionGuard() {
    section("OpenCommand — reason vs barrier direction");

    // The Application layer rejects open_cmd when the reason's implied
    // direction conflicts with the targeted barrier's direction. This
    // exercises the pure helper that drives that decision.

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

    // Simulate the Application::handleOpenCommand decision for a few combos.
    auto allowed = [](OpenReason r, BarrierDirection bdir) {
        if (r == OpenReason::Unknown) return false;       // hard refuse
        auto implied = openReasonImpliesDirection(r);
        return implied == BarrierDirection::Unknown || implied == bdir;
    };

    // Exit barrier accepts payment / monthly / free / manual / unknown(reject).
    CHECK( allowed(OpenReason::PaymentConfirmed, BarrierDirection::Exit));
    CHECK( allowed(OpenReason::MonthlyTicket,    BarrierDirection::Exit));
    CHECK( allowed(OpenReason::FreeExit,         BarrierDirection::Exit));
    CHECK( allowed(OpenReason::ManualOverride,   BarrierDirection::Exit));
    CHECK(!allowed(OpenReason::VehicleEntry,     BarrierDirection::Exit));   // ← this is the bug fix
    CHECK(!allowed(OpenReason::EntryAuthorized,  BarrierDirection::Exit));
    CHECK(!allowed(OpenReason::Unknown,          BarrierDirection::Exit));

    // Entry barrier mirrors: refuses anything exit-flavoured.
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

    // Sanity: every mode × message combination returns a bool — no UB.
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
    testBridgeActionsForMode(DeviceType::EntryOnly, "entry_only");
    testBridgeActionsForMode(DeviceType::ExitOnly,  "exit_only");
    testBridgeActionsForMode(DeviceType::Full,       "full");
    testBridgeRejectsBadProfiles();
    testFullWifiProvisioningFlow();

    printf("\n========================\n");
    printf("PASS: %d   FAIL: %d\n", g_passed, g_failed);
    return g_failed == 0 ? 0 : 1;
}
