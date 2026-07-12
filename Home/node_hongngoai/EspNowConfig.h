/**
 * @file EspNowConfig.h
 * @brief Command / DeviceType / cấu hình kênh. Nguồn chuẩn: common/EspNowConfig.h
 */
#pragma once

#include <stdint.h>
#include <string.h>

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
    static constexpr uint8_t SCAN_ATTEMPTS = 5;
    static constexpr uint8_t MAX_NODE_ID   = 50;

    uint8_t channel;

    EspNowConfig() : channel(CHANNEL_MIN) {}

    static void copyBroadcast(uint8_t out[6]) {
        memset(out, 0xFF, 6);
    }

    static bool isBroadcast(const uint8_t mac[6]) {
        for (int i = 0; i < 6; ++i) {
            if (mac[i] != 0xFF) return false;
        }
        return true;
    }
};

inline bool isRawIrToken(uint32_t code) {
    return (code & 0x80000000u) != 0;
}

inline uint32_t makeRawIrToken(uint16_t pulseCount) {
    return 0x80000000u | static_cast<uint32_t>(pulseCount);
}

inline uint32_t rawIrPulseCount(uint32_t token) {
    return token & 0x7FFFFFFFu;
}

}  // namespace smarthome
