/**
 * @file DhtSensor.h
 * @brief Cảm biến DHT11 — đọc có retry, validate range, giữ last-good value.
 *        Phần cứng Hybrid: GPIO2 (ESP-01S).
 */
#pragma once

#include <Arduino.h>
#include <DHT.h>
#include <math.h>

class DhtSensor {
public:
    DhtSensor(uint8_t pin, uint8_t dhtType = DHT11)
        : dht_(pin, dhtType),
          pin_(pin),
          temperature_(0.0f),
          humidity_(0.0f),
          valid_(false) {}

    void begin() {
        dht_.begin();
    }

    /**
     * Đọc cảm biến. Nếu NaN → chờ retryDelayMs rồi đọc lần 2.
     * Validate: temp [-20, 80], humidity [0, 100].
     * @return true nếu có giá trị mới hợp lệ (cập nhật last-good).
     */
    bool read(unsigned long retryDelayMs = 150) {
        float h = dht_.readHumidity();
        float t = dht_.readTemperature();

        if (isnan(h) || isnan(t)) {
            delay(retryDelayMs);  // chỉ trong lần đọc lỗi — không nằm trong loop chính
            h = dht_.readHumidity();
            t = dht_.readTemperature();
        }

        if (isnan(h) || isnan(t) || t < -20.0f || t > 80.0f || h < 0.0f || h > 100.0f) {
            Serial.println("[CẢNH BÁO] Lỗi đọc DHT11! Kiểm tra: 1) Dây tín hiệu IO2; 2) Loại DHT11?");
            return false;
        }

        temperature_ = t;
        humidity_    = h;
        valid_       = true;
        Serial.printf("[SENSOR] Nhiệt độ: %.1f°C | Độ ẩm: %.1f%%\n", t, h);
        return true;
    }

    /** Warm-up sau cấp nguồn (DHT cần ~1.5s). */
    void warmupAndRead(unsigned long warmupMs = 1500) {
        Serial.println("[SENSOR] Đang chờ cảm biến DHT ổn định...");
        delay(warmupMs);
        read();
    }

    float temperature() const { return temperature_; }
    float humidity() const { return humidity_; }
    bool hasValidReading() const { return valid_; }
    uint8_t pin() const { return pin_; }

    /** true nếu chưa có mẫu hợp lệ (0.0 hoặc NaN theo behavior firmware cũ). */
    bool needsInitialRead() const {
        return !valid_ || temperature_ == 0.0f || isnan(temperature_);
    }

private:
    DHT dht_;
    uint8_t pin_;
    float temperature_;
    float humidity_;
    bool valid_;
};
