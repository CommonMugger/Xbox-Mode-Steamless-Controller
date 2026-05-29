#pragma once
#include "StandardGamepadState.h"
#include <functional>
#include <cstdint>
#include <cstddef>

class TrackpadMouse {
public:
    using HapticCallback      = std::function<void(uint8_t strength)>;
    using MouseUpdateCallback = std::function<void(int16_t dx, int16_t dy, uint8_t buttons)>;

    static constexpr uint8_t MOUSE_BTN_LEFT  = 0x01u;
    static constexpr uint8_t MOUSE_BTN_RIGHT = 0x02u;

    void SetTrackpadEnabled(bool enabled)     { m_trackpadEnabled    = enabled; }
    void SetBackButtonsEnabled(bool enabled)  { m_backButtonsEnabled = enabled; }
    void SetUseLeftTrackpad(bool enabled)     { m_useLeftTrackpad    = enabled; }
    void SetHapticCallback(HapticCallback callback) { m_hapticCallback = std::move(callback); }
    void SetMouseUpdateCallback(MouseUpdateCallback callback) { m_mouseCallback = std::move(callback); }
    void SetFirmwareMouseEnabled(bool enabled) { m_firmwareMouseEnabled = enabled; }

    void Update(const uint8_t* buf, size_t n, const StandardGamepadState* standardState = nullptr);
    void Reset();

private:
    void SetButton(uint8_t btn, bool pressed);
    void SendMove(int16_t dx, int16_t dy);

    bool     m_trackpadEnabled    = false;
    bool     m_backButtonsEnabled = false;
    bool     m_useLeftTrackpad    = false;
    bool     m_firmwareMouseEnabled = false;

    bool     m_touching         = false;
    bool     m_clickPressActive = false;
    bool     m_rightClickActive = false;
    bool     m_prevR4           = false;
    bool     m_prevR5           = false;
    int16_t  m_prevX            = 0;
    int16_t  m_prevY            = 0;
    uint8_t  m_currentButtons   = 0;
    float    m_hapticMovAccum   = 0.0f;
    std::uint64_t m_clickPressStartTickMs = 0;
    std::uint64_t m_leftClickReleaseTickMs = 0;
    HapticCallback      m_hapticCallback;
    MouseUpdateCallback m_mouseCallback;

    static constexpr float SENSITIVITY              = 0.015f;
    static constexpr float HAPTIC_MOVE_THRESHOLD    = 120.0f;  // pixels per haptic pulse
    static constexpr uint8_t HAPTIC_MOVE_STRENGTH   = 4;
    static constexpr uint8_t HAPTIC_CLICK_STRENGTH  = 64;
    static constexpr std::uint64_t RIGHT_CLICK_HOLD_MS = 300;
    // The virtual mouse reports button state that the host samples periodically,
    // so a short click must stay held long enough to be observed across at least
    // one host poll. An instantaneous press+release would never register.
    static constexpr std::uint64_t LEFT_CLICK_HOLD_MS  = 60;
};
