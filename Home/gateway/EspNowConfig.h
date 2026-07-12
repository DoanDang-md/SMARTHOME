/**
 * @file EspNowConfig.h
 * @brief Command / DeviceType. Nguồn chuẩn: common/EspNowConfig.h
 */
#pragma once
#include <stdint.h>
#include <string.h>
#include <stdio.h>

namespace smarthome {

enum DeviceType : uint8_t {
    DEVICE_RELAY  = 1,
    DEVICE_IR     = 2,
    DEVICE_SENSOR = 3,
    DEVICE_HYBRID = 4
};

enum Command : uint8_t {
    CMD_DISCOVERY  = 0x00,
    CMD_RELAY_ON   = 0x01,
    CMD_RELAY_OFF  = 0x02,
    CMD_ACK_REPORT = 0x03,
    CMD_IR_DATA    = 0x10,
    CMD_IR_LEARN   = 0x11,
    CMD_IR_SAVE    = 0x12
};

struct EspNowConfig {
    static constexpr uint8_t CHANNEL_MIN   = 1;
    static constexpr uint8_t CHANNEL_MAX   = 13;
    static constexpr uint8_t MAX_NODE_ID   = 50;
    static constexpr int     MAX_NODES     = 20;
};

inline bool isRawIrToken(uint32_t code) {
    return (code & 0x80000000u) != 0;
}

inline bool parseMacAddress(const char* macStr, uint8_t* macArr) {
    int values[6];
    if (sscanf(macStr, "%x:%x:%x:%x:%x:%x",
               &values[0], &values[1], &values[2],
               &values[3], &values[4], &values[5]) == 6) {
        for (int i = 0; i < 6; ++i) macArr[i] = static_cast<uint8_t>(values[i]);
        return true;
    }
    return false;
}

}  // namespace smarthome
