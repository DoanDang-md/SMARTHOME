/**
 * @file EspNowPacket.h
 * @brief Wire-format gói tin ESP-NOW SmartHome — PHẢI giữ nguyên layout binary.
 *        Bản copy cho sketch (Arduino IDE). Nguồn chuẩn: common/EspNowPacket.h
 */
#pragma once

#include <stdint.h>

typedef struct __attribute__((packed)) EspNowPacket {
    uint8_t  mac[6];
    uint8_t  node_id;
    uint8_t  device_type;
    uint8_t  command;
    uint16_t seq;
    float    temperature;
    float    humidity;
    uint8_t  status;
    uint32_t ir_data;
} EspNowPacket;

typedef EspNowPacket esp_now_packet_t;
