/**
 * @file StorageManager.h
 * @brief Wrap Preferences — namespace "smarthome" cho Gateway.
 */
#pragma once

#include <Arduino.h>
#include <Preferences.h>

class StorageManager {
public:
    explicit StorageManager(const char* nsName = "smarthome")
        : nsName_(nsName), opened_(false) {}

    bool begin(bool readOnly = false) {
        opened_ = preferences_.begin(nsName_, readOnly);
        return opened_;
    }

    void end() {
        if (opened_) {
            preferences_.end();
            opened_ = false;
        }
    }

    uint32_t getUInt(const char* key, uint32_t def = 0) {
        return preferences_.getUInt(key, def);
    }
    void putUInt(const char* key, uint32_t val) { preferences_.putUInt(key, val); }

    String getString(const char* key, const String& def = String()) {
        return preferences_.getString(key, def);
    }
    void putString(const char* key, const String& val) { preferences_.putString(key, val); }

    size_t getBytes(const char* key, void* buf, size_t maxLen) {
        return preferences_.getBytes(key, buf, maxLen);
    }
    size_t putBytes(const char* key, const void* buf, size_t len) {
        return preferences_.putBytes(key, buf, len);
    }

    bool isKey(const char* key) { return preferences_.isKey(key); }
    bool remove(const char* key) { return preferences_.remove(key); }

    Preferences& raw() { return preferences_; }

private:
    const char* nsName_;
    bool opened_;
    Preferences preferences_;
};
