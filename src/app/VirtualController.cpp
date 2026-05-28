#include "VirtualController.h"
#include "logging/Log.h"
#include "steam/SteamController.h"
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable: 4505)
#endif
#include "libVIIPER/libVIIPER.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif
#include <Windows.h>
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <mutex>
#include <string>
#include <unordered_map>

namespace {

struct ViiperApi {
    HMODULE module = nullptr;
    decltype(&NewUSBServer) NewUSBServerFn = nullptr;
    decltype(&CloseUSBServer) CloseUSBServerFn = nullptr;
    decltype(&CreateUSBBus) CreateUSBBusFn = nullptr;
    decltype(&RemoveUSBBus) RemoveUSBBusFn = nullptr;
    decltype(&CreateXbox360Device) CreateXbox360DeviceFn = nullptr;
    decltype(&SetXbox360DeviceState) SetXbox360DeviceStateFn = nullptr;
    decltype(&SetXbox360RumbleCallback) SetXbox360RumbleCallbackFn = nullptr;
    decltype(&RemoveXbox360Device) RemoveXbox360DeviceFn = nullptr;
    decltype(&CreateDS4Device) CreateDS4DeviceFn = nullptr;
    decltype(&SetDS4DeviceState) SetDS4DeviceStateFn = nullptr;
    decltype(&SetDS4OutputCallback) SetDS4OutputCallbackFn = nullptr;
    decltype(&RemoveDS4Device) RemoveDS4DeviceFn = nullptr;
    bool loaded = false;
};

std::mutex g_notificationMutex;
std::unordered_map<std::uintptr_t, VirtualController*> g_targetOwners;

constexpr uint16_t XUSB_GAMEPAD_DPAD_UP          = 0x0001u;
constexpr uint16_t XUSB_GAMEPAD_DPAD_DOWN        = 0x0002u;
constexpr uint16_t XUSB_GAMEPAD_DPAD_LEFT        = 0x0004u;
constexpr uint16_t XUSB_GAMEPAD_DPAD_RIGHT       = 0x0008u;
constexpr uint16_t XUSB_GAMEPAD_START            = 0x0010u;
constexpr uint16_t XUSB_GAMEPAD_BACK             = 0x0020u;
constexpr uint16_t XUSB_GAMEPAD_LEFT_THUMB       = 0x0040u;
constexpr uint16_t XUSB_GAMEPAD_RIGHT_THUMB      = 0x0080u;
constexpr uint16_t XUSB_GAMEPAD_LEFT_SHOULDER    = 0x0100u;
constexpr uint16_t XUSB_GAMEPAD_RIGHT_SHOULDER   = 0x0200u;
constexpr uint16_t XUSB_GAMEPAD_GUIDE            = 0x0400u;
constexpr uint16_t XUSB_GAMEPAD_A                = 0x1000u;
constexpr uint16_t XUSB_GAMEPAD_B                = 0x2000u;
constexpr uint16_t XUSB_GAMEPAD_X                = 0x4000u;
constexpr uint16_t XUSB_GAMEPAD_Y                = 0x8000u;

struct XusbReport {
    uint16_t buttons = 0;
    uint8_t leftTrigger = 0;
    uint8_t rightTrigger = 0;
    int16_t leftX = 0;
    int16_t leftY = 0;
    int16_t rightX = 0;
    int16_t rightY = 0;
};

template <typename T>
bool LoadProc(HMODULE module, const char* name, T& fn) {
    fn = reinterpret_cast<T>(GetProcAddress(module, name));
    if (!fn)
        logging::Logf("[VIIPER] Missing export: %s", name);
    return fn != nullptr;
}

std::wstring GetAppDirectory() {
    wchar_t path[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    std::wstring full(path);
    const size_t slash = full.find_last_of(L"\\/");
    if (slash == std::wstring::npos)
        return L".";
    return full.substr(0, slash);
}

ViiperApi& GetViiperApi() {
    static ViiperApi api;
    static std::once_flag once;
    std::call_once(once, [&]() {
        const std::wstring dllPath = GetAppDirectory() + L"\\libVIIPER.dll";
        api.module = LoadLibraryW(dllPath.c_str());
        if (!api.module) {
            logging::Logf("[VIIPER] LoadLibrary failed path=%s error=%lu",
                          logging::Narrow(dllPath).c_str(),
                          GetLastError());
            return;
        }

        api.loaded =
            LoadProc(api.module, "NewUSBServer", api.NewUSBServerFn) &&
            LoadProc(api.module, "CloseUSBServer", api.CloseUSBServerFn) &&
            LoadProc(api.module, "CreateUSBBus", api.CreateUSBBusFn) &&
            LoadProc(api.module, "RemoveUSBBus", api.RemoveUSBBusFn) &&
            LoadProc(api.module, "CreateXbox360Device", api.CreateXbox360DeviceFn) &&
            LoadProc(api.module, "SetXbox360DeviceState", api.SetXbox360DeviceStateFn) &&
            LoadProc(api.module, "SetXbox360RumbleCallback", api.SetXbox360RumbleCallbackFn) &&
            LoadProc(api.module, "RemoveXbox360Device", api.RemoveXbox360DeviceFn) &&
            LoadProc(api.module, "CreateDS4Device", api.CreateDS4DeviceFn) &&
            LoadProc(api.module, "SetDS4DeviceState", api.SetDS4DeviceStateFn) &&
            LoadProc(api.module, "SetDS4OutputCallback", api.SetDS4OutputCallbackFn) &&
            LoadProc(api.module, "RemoveDS4Device", api.RemoveDS4DeviceFn);

        if (api.loaded)
            logging::Logf("[VIIPER] libVIIPER loaded from %s", logging::Narrow(dllPath).c_str());
    });
    return api;
}

void ViiperLogCallback(VIIPERLogLevel level, const char* message) {
    const char* levelName = "INFO";
    switch (level) {
    case VIIPER_LOG_DEBUG: levelName = "DEBUG"; break;
    case VIIPER_LOG_INFO: levelName = "INFO"; break;
    case VIIPER_LOG_WARN: levelName = "WARN"; break;
    case VIIPER_LOG_ERROR: levelName = "ERROR"; break;
    }
    logging::Logf("[VIIPER/%s] %s", levelName, message ? message : "");
}

static XusbReport Translate(const uint8_t* buf, size_t n) {
    constexpr int16_t kStickCenterDeadzone = 1024;
    XusbReport r{};
    if (!SteamController::UsesLegacyStateLayout(buf, n)) return r;

    const uint8_t b0 = buf[2];
    const uint8_t b1 = buf[3];
    const uint8_t b2 = buf[4];

    if (b0 & SteamController::BTN_A) r.buttons |= XUSB_GAMEPAD_A;
    if (b0 & SteamController::BTN_B) r.buttons |= XUSB_GAMEPAD_B;
    if (b0 & SteamController::BTN_X) r.buttons |= XUSB_GAMEPAD_X;
    if (b0 & SteamController::BTN_Y) r.buttons |= XUSB_GAMEPAD_Y;
    if (b2 & SteamController::BTN_LB) r.buttons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    if (b1 & SteamController::BTN_RB) r.buttons |= XUSB_GAMEPAD_RIGHT_SHOULDER;
    if (b0 & SteamController::BTN_MENU) r.buttons |= XUSB_GAMEPAD_START;
    if (b1 & SteamController::BTN_VIEW) r.buttons |= XUSB_GAMEPAD_BACK;
    if (b1 & SteamController::BTN_LS) r.buttons |= XUSB_GAMEPAD_LEFT_THUMB;
    if (b0 & SteamController::BTN_RS) r.buttons |= XUSB_GAMEPAD_RIGHT_THUMB;
    if (b2 & SteamController::BTN_STEAM) r.buttons |= XUSB_GAMEPAD_GUIDE;
    if (b1 & SteamController::BTN_DPAD_UP)  r.buttons |= XUSB_GAMEPAD_DPAD_UP;
    if (b1 & SteamController::BTN_DPAD_DN)  r.buttons |= XUSB_GAMEPAD_DPAD_DOWN;
    if (b1 & SteamController::BTN_DPAD_LT)  r.buttons |= XUSB_GAMEPAD_DPAD_LEFT;
    if (b1 & SteamController::BTN_DPAD_RT)  r.buttons |= XUSB_GAMEPAD_DPAD_RIGHT;

    int16_t ltRaw = 0;
    int16_t rtRaw = 0;
    memcpy(&ltRaw, buf + 6, 2);
    memcpy(&rtRaw, buf + 8, 2);
    r.leftTrigger  = static_cast<uint8_t>(std::clamp<int>(ltRaw >> 7, 0, 255));
    r.rightTrigger = static_cast<uint8_t>(std::clamp<int>(rtRaw >> 7, 0, 255));

    memcpy(&r.leftX, buf + 10, 2);
    memcpy(&r.leftY, buf + 12, 2);
    memcpy(&r.rightX, buf + 14, 2);
    memcpy(&r.rightY, buf + 16, 2);
    if (std::abs(static_cast<int>(r.leftX)) < kStickCenterDeadzone) r.leftX = 0;
    if (std::abs(static_cast<int>(r.leftY)) < kStickCenterDeadzone) r.leftY = 0;
    if (std::abs(static_cast<int>(r.rightX)) < kStickCenterDeadzone) r.rightX = 0;
    if (std::abs(static_cast<int>(r.rightY)) < kStickCenterDeadzone) r.rightY = 0;
    return r;
}

static XusbReport Translate(const StandardGamepadState& state) {
    XusbReport r{};
    if (!state.connected)
        return r;

    if (state.a) r.buttons |= XUSB_GAMEPAD_A;
    if (state.b) r.buttons |= XUSB_GAMEPAD_B;
    if (state.x) r.buttons |= XUSB_GAMEPAD_X;
    if (state.y) r.buttons |= XUSB_GAMEPAD_Y;
    if (state.leftShoulder) r.buttons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    if (state.rightShoulder) r.buttons |= XUSB_GAMEPAD_RIGHT_SHOULDER;
    if (state.start) r.buttons |= XUSB_GAMEPAD_START;
    if (state.back) r.buttons |= XUSB_GAMEPAD_BACK;
    if (state.leftStick) r.buttons |= XUSB_GAMEPAD_LEFT_THUMB;
    if (state.rightStick) r.buttons |= XUSB_GAMEPAD_RIGHT_THUMB;
    if (state.guide) r.buttons |= XUSB_GAMEPAD_GUIDE;
    if (state.dpadUp) r.buttons |= XUSB_GAMEPAD_DPAD_UP;
    if (state.dpadDown) r.buttons |= XUSB_GAMEPAD_DPAD_DOWN;
    if (state.dpadLeft) r.buttons |= XUSB_GAMEPAD_DPAD_LEFT;
    if (state.dpadRight) r.buttons |= XUSB_GAMEPAD_DPAD_RIGHT;
    r.leftTrigger = state.leftTrigger;
    r.rightTrigger = state.rightTrigger;
    r.leftX = state.leftX;
    r.leftY = state.leftY;
    r.rightX = state.rightX;
    r.rightY = state.rightY;
    return r;
}

void ApplyPaddleMapping(XusbReport& report, PaddleMapping mapping) {
    switch (mapping) {
    case PaddleMapping::None: return;
    case PaddleMapping::A: report.buttons |= XUSB_GAMEPAD_A; return;
    case PaddleMapping::B: report.buttons |= XUSB_GAMEPAD_B; return;
    case PaddleMapping::X: report.buttons |= XUSB_GAMEPAD_X; return;
    case PaddleMapping::Y: report.buttons |= XUSB_GAMEPAD_Y; return;
    case PaddleMapping::LeftShoulder: report.buttons |= XUSB_GAMEPAD_LEFT_SHOULDER; return;
    case PaddleMapping::RightShoulder: report.buttons |= XUSB_GAMEPAD_RIGHT_SHOULDER; return;
    case PaddleMapping::View: report.buttons |= XUSB_GAMEPAD_BACK; return;
    case PaddleMapping::Menu: report.buttons |= XUSB_GAMEPAD_START; return;
    case PaddleMapping::LeftThumb: report.buttons |= XUSB_GAMEPAD_LEFT_THUMB; return;
    case PaddleMapping::RightThumb: report.buttons |= XUSB_GAMEPAD_RIGHT_THUMB; return;
    case PaddleMapping::Guide: report.buttons |= XUSB_GAMEPAD_GUIDE; return;
    case PaddleMapping::DPadUp: report.buttons |= XUSB_GAMEPAD_DPAD_UP; return;
    case PaddleMapping::DPadRight: report.buttons |= XUSB_GAMEPAD_DPAD_RIGHT; return;
    case PaddleMapping::DPadDown: report.buttons |= XUSB_GAMEPAD_DPAD_DOWN; return;
    case PaddleMapping::DPadLeft: report.buttons |= XUSB_GAMEPAD_DPAD_LEFT; return;
    }
}

PaddleMapping ResolvePaddleGamepadMapping(PaddleMapping menuMapping, const PaddleAction& action) {
    switch (action.type) {
    case PaddleActionType::UseMenuMapping:
        return menuMapping;
    case PaddleActionType::None:
    case PaddleActionType::KeyChord:
    case PaddleActionType::Macro:
        return PaddleMapping::None;
    case PaddleActionType::Gamepad:
        return action.gamepadMapping;
    }
    return PaddleMapping::None;
}

static bool IsPaddlePressed(const uint8_t* buf, int paddleIndex) {
    const uint8_t b0 = buf[2];
    const uint8_t b1 = buf[3];
    const uint8_t b2 = buf[4];
    switch (paddleIndex) {
    case 0: return (b2 & SteamController::BTN_L4) != 0;
    case 1: return (b2 & SteamController::BTN_L5) != 0;
    case 2: return (b0 & SteamController::BTN_R4) != 0;
    case 3: return (b1 & SteamController::BTN_R5) != 0;
    default: return (b0 & SteamController::BTN_QAM) != 0;
    }
}

void ApplyPaddleMappings(XusbReport& report, const uint8_t* buf, size_t n,
                         const StandardGamepadState* standardState,
                         const PaddleMappings& mappings,
                         const PaddleActionBindings& actions,
                         bool prevPressed[5]) {
    const PaddleMapping menuMappings[] = {
        mappings.l4, mappings.l5, mappings.r4, mappings.r5, mappings.qam
    };
    const PaddleAction actionList[] = {
        actions.l4, actions.l5, actions.r4, actions.r5, actions.qam
    };

    for (int i = 0; i < 5; ++i) {
        bool pressed = false;
        if (standardState && standardState->connected) {
            switch (i) {
            case 0: pressed = standardState->leftPaddle1; break;
            case 1: pressed = standardState->leftPaddle2; break;
            case 2: pressed = standardState->rightPaddle1; break;
            case 3: pressed = standardState->rightPaddle2; break;
            default: pressed = standardState->misc1 || standardState->touchpadButton; break;
            }
        } else if (SteamController::UsesLegacyStateLayout(buf, n)) {
            pressed = IsPaddlePressed(buf, i);
        }

        const PaddleAction& action = actionList[i];
        const PaddleMapping mapping = ResolvePaddleGamepadMapping(menuMappings[i], action);
        const bool active = pressed && (
            action.rapidFire ? ((GetTickCount64() / 90) % 2 == 0) :
            (action.type == PaddleActionType::UseMenuMapping || action.type == PaddleActionType::Gamepad));
        if (active)
            ApplyPaddleMapping(report, mapping);

        prevPressed[i] = pressed;
    }
}

int8_t ScaleThumbToDs4(int16_t value) {
    const int clamped = std::clamp(static_cast<int>(std::lround(static_cast<double>(value) * 127.0 / 32767.0)), -128, 127);
    return static_cast<int8_t>(clamped);
}

uint8_t BuildDs4Dpad(const XusbReport& xusb) {
    const bool up = (xusb.buttons & XUSB_GAMEPAD_DPAD_UP) != 0;
    const bool right = (xusb.buttons & XUSB_GAMEPAD_DPAD_RIGHT) != 0;
    const bool down = (xusb.buttons & XUSB_GAMEPAD_DPAD_DOWN) != 0;
    const bool left = (xusb.buttons & XUSB_GAMEPAD_DPAD_LEFT) != 0;

    if (up && right) return DS4_DPAD_UP_RIGHT;
    if (right && down) return DS4_DPAD_DOWN_RIGHT;
    if (down && left) return DS4_DPAD_DOWN_LEFT;
    if (left && up) return DS4_DPAD_UP_LEFT;
    if (up) return DS4_DPAD_UP;
    if (right) return DS4_DPAD_RIGHT;
    if (down) return DS4_DPAD_DOWN;
    if (left) return DS4_DPAD_LEFT;
    return DS4_DPAD_NEUTRAL;
}

DS4DeviceState TranslateDs4(const XusbReport& xusb) {
    DS4DeviceState ds4{};
    ds4.LX = ScaleThumbToDs4(xusb.leftX);
    ds4.LY = ScaleThumbToDs4(xusb.leftY);
    ds4.RX = ScaleThumbToDs4(xusb.rightX);
    ds4.RY = ScaleThumbToDs4(xusb.rightY);
    ds4.DPad = BuildDs4Dpad(xusb);
    ds4.L2 = xusb.leftTrigger;
    ds4.R2 = xusb.rightTrigger;
    ds4.AccelZ = static_cast<int16_t>(-9.81f * 512.0f);

    if (xusb.buttons & XUSB_GAMEPAD_BACK) ds4.Buttons |= DS4_BUTTON_SHARE;
    if (xusb.buttons & XUSB_GAMEPAD_START) ds4.Buttons |= DS4_BUTTON_OPTIONS;
    if (xusb.buttons & XUSB_GAMEPAD_LEFT_THUMB) ds4.Buttons |= DS4_BUTTON_L3;
    if (xusb.buttons & XUSB_GAMEPAD_RIGHT_THUMB) ds4.Buttons |= DS4_BUTTON_R3;
    if (xusb.buttons & XUSB_GAMEPAD_LEFT_SHOULDER) ds4.Buttons |= DS4_BUTTON_L1;
    if (xusb.buttons & XUSB_GAMEPAD_RIGHT_SHOULDER) ds4.Buttons |= DS4_BUTTON_R1;
    if (xusb.buttons & XUSB_GAMEPAD_GUIDE) ds4.Buttons |= DS4_BUTTON_PS;
    if (xusb.buttons & XUSB_GAMEPAD_A) ds4.Buttons |= DS4_BUTTON_CROSS;
    if (xusb.buttons & XUSB_GAMEPAD_B) ds4.Buttons |= DS4_BUTTON_CIRCLE;
    if (xusb.buttons & XUSB_GAMEPAD_X) ds4.Buttons |= DS4_BUTTON_SQUARE;
    if (xusb.buttons & XUSB_GAMEPAD_Y) ds4.Buttons |= DS4_BUTTON_TRIANGLE;
    if (xusb.leftTrigger > 0) ds4.Buttons |= DS4_BUTTON_L2;
    if (xusb.rightTrigger > 0) ds4.Buttons |= DS4_BUTTON_R2;
    return ds4;
}

} // namespace

VirtualController::VirtualController(EmulationMode mode, PaddleMappings paddleMappings,
                                     PaddleActionBindings paddleActions,
                                     RumbleCallback onRumble)
    : m_mode(mode), m_paddleMappings(paddleMappings), m_paddleActions(std::move(paddleActions)),
      m_onRumble(std::move(onRumble)) {
    ViiperApi& api = GetViiperApi();
    if (!api.loaded) {
        logging::Logf("[VIIPER] API not available");
        m_driverMissing = true;
        return;
    }

    USBServerConfig config{};
    config.addr = const_cast<char*>("localhost:3245");
    config.connection_timeout_ms = 30000;
    config.device_handler_connect_timeout_ms = 5000;
    config.write_batch_flush_interval_ms = 1;

    if (!api.NewUSBServerFn(&config, &m_serverHandle, &ViiperLogCallback)) {
        logging::Logf("[VIIPER] NewUSBServer failed");
        m_driverMissing = true;
        return;
    }

    if (!api.CreateUSBBusFn(m_serverHandle, &m_busId)) {
        logging::Logf("[VIIPER] CreateUSBBus failed; USBIP client/driver may be unavailable");
        m_driverMissing = true;
        api.CloseUSBServerFn(m_serverHandle);
        m_serverHandle = 0;
        return;
    }

    bool ok = false;
    if (m_mode == EmulationMode::DualShock4) {
        ok = api.CreateDS4DeviceFn(m_serverHandle, &m_deviceHandle, m_busId, true, 0, 0) != 0;
        if (ok)
            ok = api.SetDS4OutputCallbackFn(m_deviceHandle, &VirtualController::ViiperDs4OutputCallback) != 0;
    } else {
        ok = api.CreateXbox360DeviceFn(m_serverHandle, &m_deviceHandle, m_busId, true, 0, 0, 0) != 0;
        if (ok)
            ok = api.SetXbox360RumbleCallbackFn(m_deviceHandle, &VirtualController::ViiperXboxRumbleCallback) != 0;
    }

    if (!ok) {
        logging::Logf("[VIIPER] Device creation/register callback failed mode=%d", static_cast<int>(m_mode));
        m_driverMissing = true;
        if (m_mode == EmulationMode::DualShock4 && m_deviceHandle)
            api.RemoveDS4DeviceFn(m_deviceHandle);
        if (m_mode == EmulationMode::Xbox360 && m_deviceHandle)
            api.RemoveXbox360DeviceFn(m_deviceHandle);
        if (m_serverHandle)
            api.CloseUSBServerFn(m_serverHandle);
        m_deviceHandle = 0;
        m_serverHandle = 0;
        return;
    }

    {
        std::lock_guard<std::mutex> lock(g_notificationMutex);
        g_targetOwners[m_deviceHandle] = this;
    }

    logging::Logf("[VIIPER] Virtual %s controller connected bus=%u handle=%llu",
                  m_mode == EmulationMode::DualShock4 ? "DualShock 4" : "Xbox 360",
                  m_busId,
                  static_cast<unsigned long long>(m_deviceHandle));
    m_valid = true;
}

VirtualController::~VirtualController() {
    logging::Logf("[VIIPER] VirtualController dtor valid=%d", m_valid ? 1 : 0);
    ViiperApi& api = GetViiperApi();

    if (m_deviceHandle) {
        std::lock_guard<std::mutex> lock(g_notificationMutex);
        g_targetOwners.erase(m_deviceHandle);
    }

    if (api.loaded && m_deviceHandle) {
        if (m_mode == EmulationMode::DualShock4) {
            api.SetDS4OutputCallbackFn(m_deviceHandle, nullptr);
            api.RemoveDS4DeviceFn(m_deviceHandle);
        } else {
            api.SetXbox360RumbleCallbackFn(m_deviceHandle, nullptr);
            api.RemoveXbox360DeviceFn(m_deviceHandle);
        }
    }

    if (api.loaded && m_serverHandle)
        api.CloseUSBServerFn(m_serverHandle);
}

void VirtualController::Update(const uint8_t* buf, size_t n, const StandardGamepadState* standardState) {
    if (!m_valid) return;

    XusbReport xusb{};
    if (standardState && standardState->connected) {
        xusb = Translate(*standardState);
        if (!m_loggedSdlState) {
            logging::Logf("[SDL] Using SDL standard gamepad state for virtual controller translation");
            m_loggedSdlState = true;
        }
    } else {
        xusb = Translate(buf, n);
        m_loggedSdlState = false;
    }

    ApplyPaddleMappings(xusb, buf, n, standardState, m_paddleMappings, m_paddleActions, m_prevPaddlePressed);

    ViiperApi& api = GetViiperApi();
    if (m_mode == EmulationMode::DualShock4) {
        DS4DeviceState state = TranslateDs4(xusb);
        api.SetDS4DeviceStateFn(m_deviceHandle, state);
    } else {
        Xbox360DeviceState state{};
        state.Buttons = xusb.buttons;
        state.LT = xusb.leftTrigger;
        state.RT = xusb.rightTrigger;
        state.LX = xusb.leftX;
        state.LY = xusb.leftY;
        state.RX = xusb.rightX;
        state.RY = xusb.rightY;
        api.SetXbox360DeviceStateFn(m_deviceHandle, state);
    }
}

void VirtualController::ViiperXboxRumbleCallback(std::uintptr_t handle, uint8_t leftMotor, uint8_t rightMotor) {
    std::lock_guard<std::mutex> lock(g_notificationMutex);
    auto it = g_targetOwners.find(handle);
    if (it == g_targetOwners.end() || !it->second->m_onRumble)
        return;
    it->second->m_onRumble(leftMotor, rightMotor);
}

void VirtualController::ViiperDs4OutputCallback(std::uintptr_t handle, uint8_t rumbleSmall, uint8_t rumbleLarge,
                                                uint8_t ledRed, uint8_t ledGreen, uint8_t ledBlue,
                                                uint8_t flashOn, uint8_t flashOff) {
    (void)ledRed;
    (void)ledGreen;
    (void)ledBlue;
    (void)flashOn;
    (void)flashOff;

    std::lock_guard<std::mutex> lock(g_notificationMutex);
    auto it = g_targetOwners.find(handle);
    if (it == g_targetOwners.end() || !it->second->m_onRumble)
        return;
    it->second->m_onRumble(rumbleLarge, rumbleSmall);
}
