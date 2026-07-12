/**
 * @file OfflineCache.h
 * @brief Ring buffer 100 bản ghi sensor khi offline / HTTP fail.
 */
#pragma once

#include <Arduino.h>
#include "GatewayTypes.h"

class OfflineCache {
public:
    OfflineCache()
        : mutex_(nullptr), head_(0), tail_(0), count_(0) {
        memset(buf_, 0, sizeof(buf_));
    }

    void begin() {
        mutex_ = xSemaphoreCreateMutex();
        head_ = tail_ = count_ = 0;
    }

    int count() const { return count_; }

    void push(const EspNowPacket& data) {
        if (!mutex_ || xSemaphoreTake(mutex_, portMAX_DELAY) != pdTRUE) return;
        if (count_ >= GW_CACHE_SIZE) {
            tail_ = (tail_ + 1) % GW_CACHE_SIZE;
        } else {
            count_++;
        }
        buf_[head_].packet    = data;
        buf_[head_].timestamp = millis();
        head_ = (head_ + 1) % GW_CACHE_SIZE;
        Serial.printf("[CACHE] Offline %d/%d\n", count_, GW_CACHE_SIZE);
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
        }
        xSemaphoreGive(mutex_);
        return has;
    }

private:
    cached_record_t buf_[GW_CACHE_SIZE];
    SemaphoreHandle_t mutex_;
    int head_, tail_, count_;
};
