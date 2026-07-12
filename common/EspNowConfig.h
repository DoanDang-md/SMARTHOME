/**
 * @file EspNowConfig.h
 * @brief Hằng số giao thức: command, device type, MAC broadcast, kênh WiFi.
 */
#pragma once

#include <stdint.h>
#include <string.h>

namespace smarthome {

/** Loại thiết bị (device_type trong packet) */
enum DeviceType : uint8_t {
    DEVICE_RELAY  = 1,
    DEVICE_IR     = 2,
    DEVICE_SENSOR = 3,
    DEVICE_HYBRID = 4
};

/** Mã lệnh (command trong packet) */
enum Command : uint8_t {
    CMD_DISCOVERY  = 0x00,  // Node → GW: báo danh / quét kênh
    CMD_RELAY_ON   = 0x01,  // GW → Node: bật relay
    CMD_RELAY_OFF  = 0x02,  // GW → Node: tắt relay
    CMD_ACK_REPORT = 0x03,  // 2 chiều: ACK / heartbeat / report
    CMD_IR_DATA    = 0x10,  // Node→GW: IR event | GW→Node: blast IR
    CMD_IR_LEARN   = 0x11,  // GW → Node: bật học IR
    CMD_IR_SAVE    = 0x12   // GW → Node: lưu raw IR vào Flash slot
};

/** Cấu hình kênh / peer mặc định */
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

/** Token raw IR: bit 31 = 1, 31 bit thấp = số xung */
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
