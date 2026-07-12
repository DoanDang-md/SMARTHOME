/**
 * @file NodeRegistry.h
 * @brief RAM registry node_status[20] + mutex + discovery unknown MAC.
 */
#pragma once

#include <Arduino.h>
#include <math.h>
#include "GatewayTypes.h"
#include "EspNowConfig.h"
#include "StorageManager.h"

class NodeRegistry {
public:
    NodeRegistry()
        : mutex_(nullptr),
          hasUnknown_(false),
          unknownType_(0) {
        memset(nodes_, 0, sizeof(nodes_));
        memset(unknownMac_, 0, 6);
    }

    void begin() {
        mutex_ = xSemaphoreCreateMutex();
        memset(nodes_, 0, sizeof(nodes_));
    }

    SemaphoreHandle_t mutex() const { return mutex_; }
    node_status_t* nodes() { return nodes_; }

    bool tryLock(TickType_t ticks = pdMS_TO_TICKS(50)) {
        return mutex_ && xSemaphoreTake(mutex_, ticks) == pdTRUE;
    }
    void unlock() {
        if (mutex_) xSemaphoreGive(mutex_);
    }

    /** Load peers đã lưu từ Flash vào RAM (gọi sau Storage + EspNow sẵn sàng). */
    void loadFromStorage(StorageManager& storage) {
        for (int i = 1; i <= GW_MAX_NODES; ++i) {
            String macKey = "mac_" + String(i);
            if (!storage.isKey(macKey.c_str())) continue;
            uint8_t mac[6];
            storage.getBytes(macKey.c_str(), mac, 6);
            uint8_t typeVal = static_cast<uint8_t>(storage.getUInt(("type_" + String(i)).c_str(), 1));
            if (tryLock()) {
                nodes_[i - 1].node_id     = static_cast<uint8_t>(i);
                nodes_[i - 1].device_type = typeVal;
                memcpy(nodes_[i - 1].mac, mac, 6);
                nodes_[i - 1].active      = true;
                unlock();
            }
        }
    }

    int findNextFreeId(StorageManager& storage) const {
        for (int i = 1; i <= GW_MAX_NODES; ++i) {
            String prefKey = "mac_" + String(i);
            if (!storage.isKey(prefKey.c_str())) return i;
        }
        return -1;
    }

    int getRealNodeId(const uint8_t* mac) {
        for (int i = 0; i < GW_MAX_NODES; ++i) {
            if (nodes_[i].node_id != 0 && memcmp(nodes_[i].mac, mac, 6) == 0) {
                return nodes_[i].node_id;
            }
        }
        return 0;
    }

    bool isRegisteredMac(const uint8_t* mac) const {
        for (int i = 0; i < GW_MAX_NODES; ++i) {
            if (nodes_[i].node_id != 0 && memcmp(nodes_[i].mac, mac, 6) == 0) {
                return true;
            }
        }
        return false;
    }

    void setUnknownDiscovery(const uint8_t* mac, uint8_t type) {
        memcpy(unknownMac_, mac, 6);
        unknownType_ = type;
        hasUnknown_  = true;
    }

    void clearUnknown() {
        memset(unknownMac_, 0, 6);
        hasUnknown_ = false;
    }

    const uint8_t* unknownMac() const { return unknownMac_; }
    uint8_t unknownType() const { return unknownType_; }
    bool hasUnknown() const { return hasUnknown_; }

    void updateFromPacket(const EspNowPacket& pkt) {
        // Timeout dài hơn: tránh bỏ mất temp/hum khi Web task đang đọc registry
        if (!tryLock(pdMS_TO_TICKS(200))) {
            Serial.println("[REGISTRY] updateFromPacket: không lấy được mutex!");
            return;
        }

        int targetIdx = -1;
        for (int i = 0; i < GW_MAX_NODES; ++i) {
            if (nodes_[i].node_id != 0 && memcmp(nodes_[i].mac, pkt.mac, 6) == 0) {
                targetIdx = i;
                break;
            }
        }

        // Fallback IR 0x10: gán vào node đang is_learning
        if (targetIdx == -1 && pkt.command == smarthome::CMD_IR_DATA) {
            for (int i = 0; i < GW_MAX_NODES; ++i) {
                if (nodes_[i].node_id != 0 && nodes_[i].is_learning) {
                    targetIdx = i;
                    memcpy(nodes_[i].mac, pkt.mac, 6);
                    Serial.printf("[GATEWAY AUTO-FIX] MAC cập nhật cho Node ID=%d\n",
                                  nodes_[i].node_id);
                    break;
                }
            }
        }

        if (targetIdx != -1) {
            nodes_[targetIdx].active = true;
            if (pkt.device_type != 0) {
                nodes_[targetIdx].device_type = pkt.device_type;
            }
            // Ghi nhận thời điểm BẬT/TẮT khi status đổi (relay / hybrid)
            if (pkt.status != nodes_[targetIdx].status) {
                nodes_[targetIdx].status = pkt.status;
                nodes_[targetIdx].status_changed_ms = millis();
            } else {
                nodes_[targetIdx].status = pkt.status;
            }

            // Luôn ghi temp/hum khi packet mang giá trị hợp lệ (kể cả hybrid report 0x03)
            // Dùng khoảng rộng; NaN không ghi đè last-good
            if (!isnan(pkt.temperature) && !isnan(pkt.humidity)) {
                if (pkt.temperature != 0.0f || pkt.humidity != 0.0f
                    || pkt.device_type == smarthome::DEVICE_HYBRID
                    || pkt.device_type == smarthome::DEVICE_SENSOR
                    || nodes_[targetIdx].device_type == smarthome::DEVICE_HYBRID
                    || nodes_[targetIdx].device_type == smarthome::DEVICE_SENSOR) {
                    // Hybrid/Sensor: chấp nhận cả 0.0 nếu đang report (giữ behavior đọc được)
                    if (pkt.temperature != 0.0f || pkt.humidity != 0.0f) {
                        nodes_[targetIdx].temperature = pkt.temperature;
                        nodes_[targetIdx].humidity    = pkt.humidity;
                    }
                }
            }
            nodes_[targetIdx].last_seen = millis();

            if (pkt.command == smarthome::CMD_IR_DATA) {
                nodes_[targetIdx].device_type = smarthome::DEVICE_IR;
                nodes_[targetIdx].active      = true;

                // CHỈ chấp nhận mã mới khi đang trong phiên học.
                // Tránh: gói 0x10 trùng sau lưu / IR passive / queue trễ → UI "Đã thu được mã" lặp lại.
                if (nodes_[targetIdx].is_learning) {
                    if (pkt.ir_data != 0) {
                        nodes_[targetIdx].last_ir_data = pkt.ir_data;
                    }
                    nodes_[targetIdx].is_learning      = false;
                    nodes_[targetIdx].learn_started_ms = 0;

                    uint32_t pulses = pkt.ir_data & 0x7FFFFFFFu;
                    Serial.printf("\n=======================================================\n");
                    Serial.printf("[GATEWAY IR] >>> NODE ID=%d ĐÃ HỌC XONG LỆNH REMOTE! <<<\n",
                                  nodes_[targetIdx].node_id);
                    Serial.printf(" ├─ Token: 0x%08X\n", static_cast<unsigned>(pkt.ir_data));
                    if (smarthome::isRawIrToken(pkt.ir_data)) {
                        Serial.printf(" ├─ Raw IR | %u xung\n", static_cast<unsigned>(pulses));
                    } else {
                        Serial.printf(" ├─ NEC/RC5\n");
                    }
                    Serial.printf("=======================================================\n\n");
                } else if (nodes_[targetIdx].last_ir_data != 0
                           && pkt.ir_data == nodes_[targetIdx].last_ir_data) {
                    // Trùng mã đang chờ lưu (gửi 2 lần từ node) — bỏ qua
                    Serial.printf("[GATEWAY IR] Node ID=%d bỏ qua 0x10 trùng (đang chờ lưu)\n",
                                  nodes_[targetIdx].node_id);
                } else {
                    Serial.printf("[GATEWAY IR] Node ID=%d bỏ qua 0x10 ngoài phiên học (0x%08X)\n",
                                  nodes_[targetIdx].node_id,
                                  static_cast<unsigned>(pkt.ir_data));
                }
            } else if (pkt.command == smarthome::CMD_ACK_REPORT
                       || pkt.command == smarthome::CMD_DISCOVERY) {
                // Heartbeat: không đụng last_ir_data / is_learning
                if (nodes_[targetIdx].device_type == smarthome::DEVICE_HYBRID
                    || nodes_[targetIdx].device_type == smarthome::DEVICE_SENSOR
                    || pkt.device_type == smarthome::DEVICE_HYBRID
                    || pkt.device_type == smarthome::DEVICE_SENSOR) {
                    Serial.printf("[REGISTRY] Node ID=%d cập nhật T=%.1f H=%.1f type=%u\n",
                                  nodes_[targetIdx].node_id,
                                  nodes_[targetIdx].temperature,
                                  nodes_[targetIdx].humidity,
                                  nodes_[targetIdx].device_type);
                }
            }
        } else if (pkt.command == smarthome::CMD_IR_DATA) {
            Serial.printf("[GATEWAY LỖI] IR 0x10 từ MAC chưa đăng ký: %02X:%02X:%02X:%02X:%02X:%02X\n",
                          pkt.mac[0], pkt.mac[1], pkt.mac[2],
                          pkt.mac[3], pkt.mac[4], pkt.mac[5]);
        } else if (pkt.command == smarthome::CMD_ACK_REPORT) {
            Serial.printf("[REGISTRY] Report 0x03 không khớp MAC registry: %02X:%02X:%02X:%02X:%02X:%02X "
                          "(type=%u T=%.1f H=%.1f)\n",
                          pkt.mac[0], pkt.mac[1], pkt.mac[2],
                          pkt.mac[3], pkt.mac[4], pkt.mac[5],
                          pkt.device_type, pkt.temperature, pkt.humidity);
        }
        unlock();
    }

    void registerSlot(int id, const uint8_t mac[6], uint8_t type, bool active) {
        if (id < 1 || id > GW_MAX_NODES) return;
        if (!tryLock()) return;
        nodes_[id - 1].node_id     = static_cast<uint8_t>(id);
        nodes_[id - 1].device_type = type;
        memcpy(nodes_[id - 1].mac, mac, 6);
        nodes_[id - 1].active      = active;
        unlock();
    }

    void clearSlot(int id) {
        if (id < 1 || id > GW_MAX_NODES) return;
        if (!tryLock()) return;
        memset(&nodes_[id - 1], 0, sizeof(node_status_t));
        unlock();
    }

    void setLearning(int nodeId, bool learning) {
        if (nodeId < 1 || nodeId > GW_MAX_NODES) return;
        if (!tryLock()) return;
        nodes_[nodeId - 1].is_learning = learning;
        if (learning) {
            nodes_[nodeId - 1].last_ir_data     = 0;
            nodes_[nodeId - 1].learn_started_ms = millis();
        } else {
            nodes_[nodeId - 1].learn_started_ms = 0;
        }
        unlock();
    }

    /** Sau khi lưu lệnh: xóa pending UI (theo node_id, không phụ thuộc type). */
    void clearIrLearnUi(int nodeId) {
        if (nodeId < 1 || nodeId > GW_MAX_NODES) return;
        if (!tryLock(pdMS_TO_TICKS(100))) return;
        int idx = nodeId - 1;
        nodes_[idx].last_ir_data     = 0;
        nodes_[idx].is_learning      = false;
        nodes_[idx].learn_started_ms = 0;
        Serial.printf("[GATEWAY IR] clearIrLearnUi node=%d (hết form «mã mới»)\n", nodeId);
        unlock();
    }

    /** Hết thời gian chờ remote (~35s) → tắt is_learning trên UI. */
    void expireStaleLearning(unsigned long timeoutMs = 35000UL) {
        if (!tryLock(pdMS_TO_TICKS(20))) return;
        unsigned long now = millis();
        for (int i = 0; i < GW_MAX_NODES; ++i) {
            if (!nodes_[i].is_learning || nodes_[i].learn_started_ms == 0) continue;
            if (now - nodes_[i].learn_started_ms >= timeoutMs) {
                nodes_[i].is_learning      = false;
                nodes_[i].learn_started_ms = 0;
                Serial.printf("[GATEWAY IR] Hết hạn học node ID=%d\n", nodes_[i].node_id);
            }
        }
        unlock();
    }

    void setRelayStatus(int nodeId, uint8_t status) {
        if (nodeId < 1 || nodeId > GW_MAX_NODES) return;
        if (!tryLock()) return;
        if (nodes_[nodeId - 1].status != status) {
            nodes_[nodeId - 1].status_changed_ms = millis();
        }
        nodes_[nodeId - 1].status = status;
        unlock();
    }

    /** Tìm slot đã lưu MAC này (-1 nếu chưa có). */
    int findIdByMac(const uint8_t* mac) const {
        if (mac == nullptr) return -1;
        for (int i = 0; i < GW_MAX_NODES; ++i) {
            if (nodes_[i].node_id != 0 && memcmp(nodes_[i].mac, mac, 6) == 0) {
                return nodes_[i].node_id;
            }
        }
        return -1;
    }

private:
    node_status_t nodes_[GW_MAX_NODES];
    SemaphoreHandle_t mutex_;
    uint8_t unknownMac_[6];
    uint8_t unknownType_;
    bool hasUnknown_;
};
