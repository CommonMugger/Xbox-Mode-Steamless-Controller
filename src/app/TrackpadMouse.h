#pragma once
#include "StandardGamepadState.h"
#include <functional>
#include <cstdint>
#include <cstddef>

class TrackpadMouse {
public:
    using HapticCallback = std::function<void()>;

    void SetTrackpadEnabled(bool enabled)     { m_trackpadEnabled    = enabled; }
    void SetBackButtonsEnabled(bool enabled)  { m_backButtonsEnabled = enabled; }
    void SetUseLeftTrackpad(bool enabled)     { m_useLeftTrackpad    = enabled; }
    void SetHapticCallback(HapticCallback callback) { m_hapticCallback = std::move(callback); }
    void SetFirmwareMouseEnabled(bool enabled) { m_firmwareMouseEnabled = enabled; }

    void Update(const uint8_t* buf, size_t n, const StandardGamepadState* standardState = nullptr);
    void Reset();

private:
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
    std::uint64_t m_clickPressStartTickMs = 0;
    HapticCallback m_hapticCallback;

    static constexpr float SENSITIVITY = 0.015f;
    static constexpr std::uint64_t RIGHT_CLICK_HOLD_MS = 300;
};
