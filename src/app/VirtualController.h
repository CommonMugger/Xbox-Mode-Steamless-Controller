#pragma once
#include "StandardGamepadState.h"
#include <array>
#include <cstdint>
#include <cstddef>
#include <functional>
#include <mutex>
#include <vector>

enum class EmulationMode {
    Xbox360 = 0,
    DualShock4 = 1,
};

enum class PaddleMapping {
    None = 0,
    A,
    B,
    X,
    Y,
    LeftShoulder,
    RightShoulder,
    View,
    Menu,
    LeftThumb,
    RightThumb,
    Guide,
    DPadUp,
    DPadRight,
    DPadDown,
    DPadLeft,
};

struct PaddleMappings {
    PaddleMapping l4 = PaddleMapping::None;
    PaddleMapping l5 = PaddleMapping::None;
    PaddleMapping r4 = PaddleMapping::None;
    PaddleMapping r5 = PaddleMapping::None;
    PaddleMapping qam = PaddleMapping::None;
};

enum class PaddleActionType {
    UseMenuMapping = 0,
    None,
    Gamepad,
    KeyChord,
    Macro,
};

struct PaddleAction {
    PaddleActionType type = PaddleActionType::UseMenuMapping;
    PaddleMapping gamepadMapping = PaddleMapping::None;
    bool rapidFire = false;
    std::vector<uint16_t> chord;
    std::vector<std::vector<uint16_t>> macroSteps;
};

struct PaddleActionBindings {
    PaddleAction l4;
    PaddleAction l5;
    PaddleAction r4;
    PaddleAction r5;
    PaddleAction qam;
};

class VirtualController {
public:
    using RumbleCallback = std::function<void(uint8_t largeMotor, uint8_t smallMotor)>;

    explicit VirtualController(EmulationMode mode = EmulationMode::Xbox360,
                               PaddleMappings paddleMappings = {},
                               PaddleActionBindings paddleActions = {},
                               RumbleCallback onRumble = {});
    ~VirtualController();
    VirtualController(const VirtualController&) = delete;
    VirtualController& operator=(const VirtualController&) = delete;

    bool IsValid()          const { return m_valid; }
    bool IsDriverMissing()  const { return m_driverMissing; }
    EmulationMode GetMode() const { return m_mode; }
    void SetPaddleMappings(PaddleMappings mappings) { m_paddleMappings = mappings; }
    void SetPaddleActions(PaddleActionBindings actions) { m_paddleActions = std::move(actions); }

    void Update(const uint8_t* buf, size_t n, const StandardGamepadState* standardState = nullptr);
    void UpdateMouse(int16_t dx, int16_t dy, uint8_t buttons);
    void KeyChordDown(const std::vector<uint16_t>& vkChord);
    void KeyChordUp(const std::vector<uint16_t>& vkChord);

private:
    static void ViiperXboxRumbleCallback(std::uintptr_t handle, uint8_t leftMotor, uint8_t rightMotor);
    static void ViiperDs4OutputCallback(std::uintptr_t handle, uint8_t rumbleSmall, uint8_t rumbleLarge,
                                        uint8_t ledRed, uint8_t ledGreen, uint8_t ledBlue,
                                        uint8_t flashOn, uint8_t flashOff);

    void* m_module        = nullptr;
    std::uintptr_t m_serverHandle = 0;
    std::uintptr_t m_deviceHandle = 0;
    std::uintptr_t m_mouseHandle    = 0;
    std::uintptr_t m_keyboardHandle = 0;
    uint32_t m_busId = 0;
    bool  m_valid        = false;
    bool  m_driverMissing = false;
    EmulationMode m_mode = EmulationMode::Xbox360;
    PaddleMappings m_paddleMappings{};
    PaddleActionBindings m_paddleActions{};
    bool m_prevPaddlePressed[5] = {false, false, false, false, false};
    bool m_loggedSdlState = false;
    RumbleCallback m_onRumble;
    std::mutex                 m_keyboardMutex;
    uint8_t                    m_kbModifiers = 0;
    std::array<uint8_t, 32>    m_kbBitmap{};
    void ApplyKeyVk(uint16_t vk, bool down);
};
