#pragma once
#include <cstdint>
#include <cstddef>
#include <functional>

class VirtualController {
public:
    using RumbleCallback = std::function<void(uint8_t largeMotor, uint8_t smallMotor)>;

    explicit VirtualController(RumbleCallback onRumble = {});
    ~VirtualController();
    VirtualController(const VirtualController&) = delete;
    VirtualController& operator=(const VirtualController&) = delete;

    bool IsValid()          const { return m_valid; }
    bool IsDriverMissing()  const { return m_driverMissing; }

    void Update(const uint8_t* buf, size_t n);

private:
    static void ViGEmNotification(void* client, void* target, uint8_t largeMotor, uint8_t smallMotor, uint8_t ledNumber);

    void* m_client       = nullptr;
    void* m_target       = nullptr;
    bool  m_valid        = false;
    bool  m_driverMissing = false;
    RumbleCallback m_onRumble;
};
