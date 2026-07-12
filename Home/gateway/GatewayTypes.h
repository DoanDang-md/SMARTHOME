/**
 * @file GatewayTypes.h
 * @brief Struct trạng thái node & bản ghi cache offline.
 */
#pragma once

#include <Arduino.h>
#include "EspNowPacket.h"
#include "EspNowConfig.h"

static constexpr int GW_MAX_NODES  = smarthome::EspNowConfig::MAX_NODES;
static constexpr int GW_CACHE_SIZE = 100;

typedef struct {
    bool active;
    uint8_t mac[6];
    uint8_t node_id;
    uint8_t device_type;
    uint8_t status;                  // 0=OFF, 1=ON
    float temperature;
    float humidity;
    uint32_t last_ir_data;
    bool is_learning;
    unsigned long learn_started_ms;  // millis() khi bấm học — timeout UI ~35s
    unsigned long last_seen;         // millis() lần RX gần nhất
    unsigned long status_changed_ms; // millis() lúc BẬT/TẮT (relay)
} node_status_t;

typedef struct {
    EspNowPacket packet;
    unsigned long timestamp;
} cached_record_t;
