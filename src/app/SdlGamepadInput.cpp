#define SDL_MAIN_HANDLED 1
#include <SDL3/SDL_main.h>
#include <SDL3/SDL.h>
#include "SdlGamepadInput.h"
#include "logging/Log.h"
#include "steam/SteamController.h"
#include <Windows.h>
#include <algorithm>
#include <mutex>

namespace {
std::mutex g_sdlMutex;
int g_sdlUsers = 0;
bool g_sdlMainReady = false;

int16_t InvertAxisY(Sint16 value) {
    return value == INT16_MIN ? INT16_MAX : static_cast<int16_t>(-value);
}

uint8_t ConvertTriggerAxis(Sint16 value) {
    const int clamped = (std::clamp)(static_cast<int>(value), 0, 32767);
    return static_cast<uint8_t>((clamped * 255 + 16383) / 32767);
}

void ReadTouchpadFinger(SDL_Gamepad* gamepad, int touchpadIndex,
                        bool& down, float& x, float& y, float& pressure) {
    down = false;
    x = 0.0f;
    y = 0.0f;
    pressure = 0.0f;

    if (!gamepad)
        return;

    const int fingers = SDL_GetNumGamepadTouchpadFingers(gamepad, touchpadIndex);
    if (fingers <= 0)
        return;

    for (int finger = 0; finger < fingers; ++finger) {
        bool fingerDown = false;
        float fingerX = 0.0f;
        float fingerY = 0.0f;
        float fingerPressure = 0.0f;
        if (!SDL_GetGamepadTouchpadFinger(gamepad, touchpadIndex, finger,
                                          &fingerDown, &fingerX, &fingerY, &fingerPressure)) {
            continue;
        }
        if (fingerDown) {
            down = true;
            x = fingerX;
            y = fingerY;
            pressure = fingerPressure;
            return;
        }
    }
}
}

SdlGamepadInput::SdlGamepadInput() {
    std::lock_guard<std::mutex> lock(g_sdlMutex);
    if (!g_sdlMainReady) {
        SDL_SetMainReady();
        g_sdlMainReady = true;
    }

    ++g_sdlUsers;
    SDL_SetHint(SDL_HINT_JOYSTICK_ALLOW_BACKGROUND_EVENTS, "1");
    SDL_SetHint(SDL_HINT_JOYSTICK_HIDAPI_STEAM, "1");

    if (!SDL_InitSubSystem(SDL_INIT_GAMEPAD)) {
        logging::Logf("[SDL] SDL_INIT_GAMEPAD failed: %s", SDL_GetError());
        --g_sdlUsers;
        return;
    }

    m_backendReady = true;
    logging::Logf("[SDL] Gamepad subsystem ready");
}

SdlGamepadInput::~SdlGamepadInput() {
    CloseCurrent();

    std::lock_guard<std::mutex> lock(g_sdlMutex);
    if (!m_backendReady)
        return;

    if (g_sdlUsers > 0)
        --g_sdlUsers;
    if (g_sdlUsers == 0)
        SDL_QuitSubSystem(SDL_INIT_GAMEPAD);
}

bool SdlGamepadInput::EnsureOpen() {
    if (!m_backendReady)
        return false;

    SDL_UpdateGamepads();
    if (m_gamepad && SDL_GamepadConnected(static_cast<SDL_Gamepad*>(m_gamepad)))
        return true;

    if (m_gamepad) {
        logging::Logf("[SDL] Gamepad disconnected");
        CloseCurrent();
    }

    const std::uint64_t now = GetTickCount64();
    if (now - m_lastOpenAttemptTickMs < 1000)
        return false;
    m_lastOpenAttemptTickMs = now;
    return OpenMatchingGamepad();
}

bool SdlGamepadInput::OpenMatchingGamepad() {
    int count = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&count);
    if (!ids) {
        if (!m_loggedUnavailable) {
            logging::Logf("[SDL] No SDL gamepads available: %s", SDL_GetError());
            m_loggedUnavailable = true;
        }
        return false;
    }

    SDL_Gamepad* matched = nullptr;
    for (int i = 0; i < count; ++i) {
        const SDL_JoystickID id = ids[i];
        const Uint16 vendor = SDL_GetGamepadVendorForID(id);
        const Uint16 product = SDL_GetGamepadProductForID(id);
        const char* name = SDL_GetGamepadNameForID(id);

        const bool matchesVidPid =
            vendor == SteamController::VALVE_VID &&
            (product == SteamController::SC2026_PID || product == SteamController::SC2026_DONGLE_PID);
        const bool matchesName = name && std::string(name).find("Steam Controller") != std::string::npos;
        if (!matchesVidPid && !matchesName)
            continue;

        matched = SDL_OpenGamepad(id);
        if (matched) {
            logging::Logf("[SDL] Opened gamepad name=%s vendor=0x%04X product=0x%04X path=%s",
                          name ? name : "(unknown)",
                          vendor,
                          product,
                          SDL_GetGamepadPath(matched) ? SDL_GetGamepadPath(matched) : "(none)");
            logging::Logf("[SDL] Capabilities touchpads=%d gyro=%d accel=%d",
                          SDL_GetNumGamepadTouchpads(matched),
                          SDL_GamepadHasSensor(matched, SDL_SENSOR_GYRO) ? 1 : 0,
                          SDL_GamepadHasSensor(matched, SDL_SENSOR_ACCEL) ? 1 : 0);
            break;
        }
    }

    SDL_free(ids);
    m_gamepad = matched;
    if (!m_gamepad && !m_loggedUnavailable) {
        logging::Logf("[SDL] No matching Steam Controller gamepad mapping available");
        m_loggedUnavailable = true;
    }
    if (m_gamepad)
        m_loggedUnavailable = false;
    return m_gamepad != nullptr;
}

void SdlGamepadInput::CloseCurrent() {
    if (!m_gamepad)
        return;
    SDL_CloseGamepad(static_cast<SDL_Gamepad*>(m_gamepad));
    m_gamepad = nullptr;
}

bool SdlGamepadInput::Poll(StandardGamepadState& state) {
    state = {};
    if (!EnsureOpen())
        return false;

    SDL_Gamepad* gamepad = static_cast<SDL_Gamepad*>(m_gamepad);
    state.connected = true;
    state.a = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_SOUTH);
    state.b = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_EAST);
    state.x = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_WEST);
    state.y = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_NORTH);
    state.back = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK);
    state.guide = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_GUIDE);
    state.start = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START);
    state.leftStick = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_STICK);
    state.rightStick = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_STICK);
    state.leftShoulder = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_SHOULDER);
    state.rightShoulder = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER);
    state.dpadUp = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_UP);
    state.dpadDown = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_DOWN);
    state.dpadLeft = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_LEFT);
    state.dpadRight = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_DPAD_RIGHT);
    state.leftX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTX);
    state.leftY = InvertAxisY(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFTY));
    state.rightX = SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTX);
    state.rightY = InvertAxisY(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHTY));
    state.leftTrigger = ConvertTriggerAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_LEFT_TRIGGER));
    state.rightTrigger = ConvertTriggerAxis(SDL_GetGamepadAxis(gamepad, SDL_GAMEPAD_AXIS_RIGHT_TRIGGER));
    state.misc1 = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_MISC1);
    state.misc2 = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_MISC2);
    state.misc3 = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_MISC3);
    state.misc4 = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_MISC4);
    state.misc5 = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_MISC5);
    state.misc6 = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_MISC6);
    state.leftPaddle1 = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_PADDLE1);
    state.leftPaddle2 = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_LEFT_PADDLE2);
    state.rightPaddle1 = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE1);
    state.rightPaddle2 = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_RIGHT_PADDLE2);
    state.touchpadButton = SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_TOUCHPAD);
    state.touchpadCount = SDL_GetNumGamepadTouchpads(gamepad);
    if (state.touchpadCount > 0)
        ReadTouchpadFinger(gamepad, 0, state.touchpad0Down, state.touchpad0X, state.touchpad0Y, state.touchpad0Pressure);
    if (state.touchpadCount > 1)
        ReadTouchpadFinger(gamepad, 1, state.touchpad1Down, state.touchpad1X, state.touchpad1Y, state.touchpad1Pressure);
    return true;
}
