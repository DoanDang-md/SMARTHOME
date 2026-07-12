/**
 * @file RelayController.h
 * @brief Điều khiển GPIO relay + đồng bộ trạng thái với StorageManager.
 *        Hỗ trợ active-HIGH (mặc định) hoặc active-LOW (module opto thường gặp).
 */
#pragma once

#include <Arduino.h>
#include "StorageManager.h"

class RelayController {
public:
    RelayController(uint8_t pin, StorageManager* storage = nullptr)
        : pin_(pin), on_(false), activeLow_(false), storage_(storage) {}

    void begin() {
        pinMode(pin_, OUTPUT);
        applyPin();
    }

    void setActiveLow(bool activeLow) { activeLow_ = activeLow; }

    /** Khôi phục trạng thái từ Flash (gọi sau StorageManager::begin). */
    void restoreFromStorage() {
        if (storage_ != nullptr) {
            on_ = storage_->getRelay(false);
            applyPin();
        }
    }

    void setOn(bool on, bool persist = true) {
        on_ = on;
        applyPin();
        if (persist && storage_ != nullptr) {
            storage_->putRelay(on_);
        }
    }

    void turnOn(bool persist = true)  { setOn(true, persist); }
    void turnOff(bool persist = true) { setOn(false, persist); }
    void toggle(bool persist = true)  { setOn(!on_, persist); }

    bool isOn() const { return on_; }
    uint8_t statusByte() const { return on_ ? 1 : 0; }
    uint8_t pin() const { return pin_; }

    void setStorage(StorageManager* storage) { storage_ = storage; }

private:
    void applyPin() {
        // activeLow: ON → LOW, OFF → HIGH
        const bool levelHigh = activeLow_ ? !on_ : on_;
        digitalWrite(pin_, levelHigh ? HIGH : LOW);
    }

    uint8_t pin_;
    bool on_;
    bool activeLow_;
    StorageManager* storage_;
};
