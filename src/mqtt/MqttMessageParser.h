/**
 * MqttMessageParser.h
 * Stateless JSON parser for server→device messages.
 */

#pragma once

#include <QByteArray>
#include "types/MessageTypes.h"

class MqttMessageParser {
public:
    static InboundMessage parse(const QByteArray& json);
};
