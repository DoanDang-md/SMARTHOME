/**
 * @file OfflineCache.h
 * @brief Ring buffer 100 bản ghi sensor khi offline / HTTP fail.
 *
 * Coalesce: heartbeat 0x03 cùng MAC ghi đè bản cũ (tránh Hybrid 5s làm đầy 100 slot
 * khi Backend down, rồi Task_ServerSync bị kẹt flush HTTP).
 */
#pragma once

#include <Arduino.h>
#include "GatewayTypes.h"
#include "EspNowConfig.h"

class OfflineCache {
public:
    OfflineCache()
        : mutex_(nullptr), head_(0), tail_(0), count_(0), lastLogCount_(-1) {
        memset(buf_, 0, sizeof(buf_));
    }

    void begin() {
        mutex_ = xSemaphoreCreateMutex();
        head_ = tail_ = count_ = 0;
        lastLogCount_ = -1;
    }

    int count() const { return count_; }

    /**
     * @param coalesceHeartbeats true: CMD_ACK_REPORT cùng MAC → update in-place
     */
    void push(const EspNowPacket& data, bool coalesceHeartbeats = true) {
        if (!mutex_ || xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) return;

        if (coalesceHeartbeats && data.command == smarthome::CMD_ACK_REPORT) {
            for (int i = 0; i < count_; ++i) {
                const int idx = (tail_ + i) % GW_CACHE_SIZE;
                if (buf_[idx].packet.command == smarthome::CMD_ACK_REPORT
                    && memcmp(buf_[idx].packet.mac, data.mac, 6) == 0) {
                    buf_[idx].packet    = data;
                    buf_[idx].timestamp = millis();
                    xSemaphoreGive(mutex_);
                    return;
                }
            }
        }

        if (count_ >= GW_CACHE_SIZE) {
            tail_ = (tail_ + 1) % GW_CACHE_SIZE;
        } else {
            count_++;
        }
        buf_[head_].packet    = data;
        buf_[head_].timestamp = millis();
        head_ = (head_ + 1) % GW_CACHE_SIZE;

        // Log thưa: mỗi lần tăng 5 slot hoặc đầy
        if (count_ != lastLogCount_
            && (count_ == 1 || count_ % 5 == 0 || count_ >= GW_CACHE_SIZE)) {
            Serial.printf("[CACHE] Offline %d/%d (BE down hoặc HTTP fail)\n",
                          count_, GW_CACHE_SIZE);
            lastLogCount_ = count_;
        }
        xSemaphoreGive(mutex_);
    }

    bool pop(cached_record_t* record) {
        bool has = false;
        if (!mutex_ || xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) return false;
        if (count_ > 0) {
            *record = buf_[tail_];
            tail_   = (tail_ + 1) % GW_CACHE_SIZE;
            count_--;
            has = true;
            if (count_ == 0) {
                lastLogCount_ = 0;
            }
        }
        xSemaphoreGive(mutex_);
        return has;
    }

private:
    cached_record_t buf_[GW_CACHE_SIZE];
    SemaphoreHandle_t mutex_;
    int head_, tail_, count_;
    int lastLogCount_;
};
