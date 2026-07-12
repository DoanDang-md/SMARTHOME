/**
 * @file DeviceNode.h
 * @brief Base class ảo cho mọi node SmartHome.
 */
#pragma once

#include <stdint.h>
#include "EspNowConfig.h"
#include "EspNowPacket.h"

class DeviceNode {
public:
    DeviceNode(uint8_t nodeId, smarthome::DeviceType type)
        : nodeId_(nodeId), deviceType_(type) {}

    virtual ~DeviceNode() {}

    virtual void begin() = 0;
    virtual void loop() = 0;

    /** Xử lý lệnh từ Gateway (sau khi EspNowManager parse packet). */
    virtual void handleCommand(const EspNowPacket& packet) = 0;

    uint8_t nodeId() const { return nodeId_; }
    void setNodeId(uint8_t id) { nodeId_ = id; }

    smarthome::DeviceType deviceType() const { return deviceType_; }

protected:
    uint8_t nodeId_;
    smarthome::DeviceType deviceType_;
};
