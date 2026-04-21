/**
 * @file MessageFilter.h
 * @brief Device-type message dispatch gate.
 *
 * Pure function extracted from Application so self-test can exercise it
 * without spinning up Qt or MQTT. Each deployment profile accepts a
 * well-defined subset of inbound message types; everything else must be
 * silently dropped so a misrouted server message cannot crash or hang the
 * device.
 *
 *  Profile     │ VehicleInfo │ OpenCmd │ QrConfirmed │ PaySuccess │ DisplayMsg │ Reconnect
 *  ────────────┼─────────────┼─────────┼─────────────┼────────────┼────────────┼──────────
 *  EntryOnly   │     ✗       │   ✓     │     ✗       │     ✗      │     ✗      │    ✓
 *  ExitOnly    │     ✓       │   ✓     │     ✓       │     ✓      │     ✓      │    ✓
 *  Full        │     ✓       │   ✓     │     ✓       │     ✓      │     ✓      │    ✓
 *  Unknown     │     ✗       │   ✗     │     ✗       │     ✗      │     ✗      │    ✗
 */

#pragma once

#include "types/MessageTypes.h"

namespace parking {

/**
 * @return true if the given inbound message should be dispatched to its
 *         handler. false means the message must be silently dropped.
 */
[[nodiscard]] constexpr bool shouldDispatch(DeviceType mode,
                                             InboundMsgType type) noexcept
{
    switch (mode) {
    case DeviceType::EntryOnly:
        // Headless profile: only barrier open/close and reconnect are meaningful.
        return type == InboundMsgType::OpenCommand
            || type == InboundMsgType::Reconnect;

    case DeviceType::ExitOnly:
    case DeviceType::Full:
        // Full UI stack handles every server-driven message.
        return type == InboundMsgType::VehicleInfo
            || type == InboundMsgType::OpenCommand
            || type == InboundMsgType::QrPaymentConfirmed
            || type == InboundMsgType::PaymentSuccess
            || type == InboundMsgType::DisplayMessage
            || type == InboundMsgType::Reconnect;

    case DeviceType::Unknown:
    default:
        return false;
    }
}

} // namespace parking
