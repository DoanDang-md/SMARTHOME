/**
 * @file EspNowPacket.h
 * @brief Wire-format gói tin ESP-NOW SmartHome — PHẢI giữ nguyên layout binary.
 *        Mọi thiết bị (Gateway / Relay / Hybrid / IR) dùng chung định nghĩa này.
 */
#pragma once

#include <stdint.h>

/**
 * Layout packed — KHÔNG thêm/bớt/đổi thứ tự field.
 * Size phải khớp trên ESP8266 và ESP32.
 */
typedef struct __attribute__((packed)) EspNowPacket {
    uint8_t  mac[6];
    uint8_t  node_id;
    uint8_t  device_type;  // DeviceType enum
    uint8_t  command;      // Command enum
    uint16_t seq;
    float    temperature;
    float    humidity;
    uint8_t  status;       // 0=OFF, 1=ON
    uint32_t ir_data;
} EspNowPacket;

// Alias tương thích code cũ
typedef EspNowPacket esp_now_packet_t;

static_assert(sizeof(EspNowPacket) == 6 + 1 + 1 + 1 + 2 + 4 + 4 + 1 + 4,
              "EspNowPacket size changed — wire protocol broken!");
