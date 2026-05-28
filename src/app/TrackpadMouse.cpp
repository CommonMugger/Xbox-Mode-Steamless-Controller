#include "TrackpadMouse.h"
#include "steam/SteamController.h"
#include <Windows.h>
#include <cstring>

static constexpr uint8_t BTN_TP_RT_CLICK = 0x40;  // buf[4] bit 6: right pad hard press
static constexpr uint16_t TRACKPAD_CLICK_AREA_MIN = 16;

void TrackpadMouse::SetButton(uint8_t btn, bool pressed) {
    const uint8_t prev = m_currentButtons;
    if (pressed) m_currentButtons |= btn; else m_currentButtons &= ~btn;
    if (m_currentButtons != prev && m_mouseCallback)
        m_mouseCallback(0, 0, m_currentButtons);
}

void TrackpadMouse::SendMove(int16_t dx, int16_t dy) {
    if (m_mouseCallback)
        m_mouseCallback(dx, dy, m_currentButtons);
}

void TrackpadMouse::Reset() {
    if (m_rightClickActive) SetButton(MOUSE_BTN_RIGHT, false);
    if (m_prevR4) SetButton(MOUSE_BTN_LEFT, false);
    if (m_prevR5) SetButton(MOUSE_BTN_RIGHT, false);
    m_touching = false;
    m_clickPressActive = false;
    m_rightClickActive = false;
    m_prevR4 = false;
    m_prevR5 = false;
    m_prevX = 0;
    m_prevY = 0;
    m_currentButtons = 0;
    m_clickPressStartTickMs = 0;
}

void TrackpadMouse::Update(const uint8_t* buf, size_t n, const StandardGamepadState* standardState) {
    const bool hasSdlState = standardState && standardState->connected;
    const bool hasSdlTouchpad = hasSdlState && standardState->touchpadCount > 0;
    const bool hasLegacyState = SteamController::UsesLegacyStateLayout(buf, n);

    uint8_t b0 = 0;
    uint8_t b1 = 0;
    uint8_t b2 = 0;
    uint8_t b3 = 0;
    if (hasLegacyState) {
        b0 = buf[2];
        b1 = buf[3];
        b2 = buf[4];
        b3 = buf[5];
    }

    if (!m_firmwareMouseEnabled && hasSdlTouchpad && m_trackpadEnabled) {
        const int desiredTouchpad = m_useLeftTrackpad ? 0 : 1;
        const int touchpadIndex = (standardState->touchpadCount > desiredTouchpad) ? desiredTouchpad : 0;
        const bool touching = (touchpadIndex == 0) ? standardState->touchpad0Down : standardState->touchpad1Down;
        const float xNorm = (touchpadIndex == 0) ? standardState->touchpad0X : standardState->touchpad1X;
        const float yNorm = (touchpadIndex == 0) ? standardState->touchpad0Y : standardState->touchpad1Y;
        const int16_t x = static_cast<int16_t>((xNorm - 0.5f) * 65534.0f);
        const int16_t y = static_cast<int16_t>((yNorm - 0.5f) * 65534.0f);

        if (touching && m_touching) {
            const int dx = static_cast<int>(x - m_prevX);
            const int dy = -static_cast<int>(y - m_prevY);
            if (dx != 0 || dy != 0)
                SendMove(static_cast<int16_t>(dx * SENSITIVITY), static_cast<int16_t>(dy * SENSITIVITY));
        }

        if (touching) {
            m_prevX = x;
            m_prevY = y;
        }
        m_touching = touching;
    } else if (!m_firmwareMouseEnabled && hasLegacyState && m_trackpadEnabled) {
        const bool touching = m_useLeftTrackpad
            ? (b3 & SteamController::BTN_TP_LT) != 0
            : (b2 & SteamController::BTN_TP_RT) != 0;

        int16_t x = 0;
        int16_t y = 0;
        if (m_useLeftTrackpad) {
            memcpy(&x, buf + 18, 2);
            memcpy(&y, buf + 20, 2);
        } else {
            memcpy(&x, buf + 24, 2);
            memcpy(&y, buf + 26, 2);
        }

        if (touching && m_touching) {
            const int dx = static_cast<int>(x - m_prevX);
            const int dy = -static_cast<int>(y - m_prevY);
            if (dx != 0 || dy != 0)
                SendMove(static_cast<int16_t>(dx * SENSITIVITY), static_cast<int16_t>(dy * SENSITIVITY));
        }

        if (touching) {
            m_prevX = x;
            m_prevY = y;
        }
        m_touching = touching;
    }

    bool oppositePadPressed = false;
    if (hasLegacyState) {
        const bool oppositePadTouching = m_useLeftTrackpad
            ? (b2 & SteamController::BTN_TP_RT) != 0
            : (b3 & SteamController::BTN_TP_LT) != 0;
        const bool oppositePadHardClick = m_useLeftTrackpad
            ? (b2 & BTN_TP_RT_CLICK) != 0
            : (b3 & SteamController::BTN_TP_LT_CLICK) != 0;
        uint16_t oppositePadArea = 0;
        if (m_useLeftTrackpad) {
            memcpy(&oppositePadArea, buf + 28, 2);
        } else {
            memcpy(&oppositePadArea, buf + 22, 2);
        }

        oppositePadPressed = oppositePadTouching &&
                             oppositePadHardClick &&
                             oppositePadArea >= TRACKPAD_CLICK_AREA_MIN;
    }

    const std::uint64_t now = GetTickCount64();
    if (oppositePadPressed) {
        if (!m_clickPressActive) {
            m_clickPressActive = true;
            m_clickPressStartTickMs = now;
            m_rightClickActive = false;
        } else if (!m_rightClickActive && (now - m_clickPressStartTickMs) >= RIGHT_CLICK_HOLD_MS) {
            SetButton(MOUSE_BTN_RIGHT, true);
            if (m_hapticCallback)
                m_hapticCallback();
            m_rightClickActive = true;
        }
    } else if (m_clickPressActive) {
        if (m_rightClickActive) {
            SetButton(MOUSE_BTN_RIGHT, false);
        } else {
            if (m_hapticCallback)
                m_hapticCallback();
            SetButton(MOUSE_BTN_LEFT, true);
            SetButton(MOUSE_BTN_LEFT, false);
        }
        m_clickPressActive = false;
        m_rightClickActive = false;
        m_clickPressStartTickMs = 0;
    }

    if (m_backButtonsEnabled) {
        bool btn1 = false;
        bool btn2 = false;
        if (hasSdlState) {
            btn1 = m_useLeftTrackpad ? standardState->leftPaddle1 : standardState->rightPaddle1;
            btn2 = m_useLeftTrackpad ? standardState->leftPaddle2 : standardState->rightPaddle2;
        } else if (hasLegacyState) {
            btn1 = m_useLeftTrackpad
                ? (b2 & SteamController::BTN_L4) != 0
                : (b0 & SteamController::BTN_R4) != 0;
            btn2 = m_useLeftTrackpad
                ? (b2 & SteamController::BTN_L5) != 0
                : (b1 & SteamController::BTN_R5) != 0;
        }

        if (btn1 != m_prevR4) {
            SetButton(MOUSE_BTN_LEFT, btn1);
            m_prevR4 = btn1;
        }
        if (btn2 != m_prevR5) {
            SetButton(MOUSE_BTN_RIGHT, btn2);
            m_prevR5 = btn2;
        }
    }
}
