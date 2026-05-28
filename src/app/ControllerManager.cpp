#include "ControllerManager.h"
#include "VirtualController.h"
#include "logging/Log.h"
#include "steam/SteamController.h"
#include <memory>
#include <vector>

namespace {
void AppendChordToken(std::wstring& text, const wchar_t* token) {
    if (!text.empty())
        text += L"+";
    text += token;
}

void AppendStandardChordTokens(std::wstring& chord, const StandardGamepadState& state) {
    if (state.a) AppendChordToken(chord, L"A");
    if (state.b) AppendChordToken(chord, L"B");
    if (state.x) AppendChordToken(chord, L"X");
    if (state.y) AppendChordToken(chord, L"Y");
    if (state.leftShoulder) AppendChordToken(chord, L"LB");
    if (state.rightShoulder) AppendChordToken(chord, L"RB");
    if (state.leftTrigger > 16) AppendChordToken(chord, L"LT");
    if (state.rightTrigger > 16) AppendChordToken(chord, L"RT");
    if (state.start) AppendChordToken(chord, L"MENU");
    if (state.back) AppendChordToken(chord, L"VIEW");
    if (state.guide) AppendChordToken(chord, L"GUIDE");
    if (state.leftStick) AppendChordToken(chord, L"L3");
    if (state.rightStick) AppendChordToken(chord, L"R3");
    if (state.dpadUp) AppendChordToken(chord, L"DPADUP");
    if (state.dpadRight) AppendChordToken(chord, L"DPADRIGHT");
    if (state.dpadDown) AppendChordToken(chord, L"DPADDOWN");
    if (state.dpadLeft) AppendChordToken(chord, L"DPADLEFT");
}

void SendKeyboardKeyUp(WORD vk) {
    INPUT input{};
    input.type = INPUT_KEYBOARD;
    input.ki.wVk = vk;
    input.ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(1, &input, sizeof(INPUT));
}

void ReleasePotentialShellKeys() {
    SendKeyboardKeyUp(VK_LWIN);
    SendKeyboardKeyUp(VK_RWIN);
    SendKeyboardKeyUp(VK_TAB);
}
}

static std::unique_ptr<SteamController> g_ctrl;

namespace {
constexpr bool kEnableFirmwareTrackpadMouse = false;
}

ControllerManager::ControllerManager(StateChangedFn onStateChanged)
    : m_onStateChanged(std::move(onStateChanged))
{
    logging::Logf("[ControllerManager] ctor");
    TryOpen();
}

ControllerManager::~ControllerManager() {
    Close(/*restoreLizard=*/true);
}

void ControllerManager::OnDeviceChange() {
    const std::uint64_t now = GetTickCount64();
    const bool shouldLog =
        m_connected != m_lastDeviceChangeLogConnected ||
        m_gameModeActive != m_lastDeviceChangeLogGameMode ||
        now - m_lastDeviceChangeLogTickMs >= 5000;
    if (shouldLog) {
        logging::Logf("[ControllerManager] OnDeviceChange connected=%d gameMode=%d",
                      m_connected ? 1 : 0, m_gameModeActive ? 1 : 0);
        m_lastDeviceChangeLogTickMs = now;
        m_lastDeviceChangeLogConnected = m_connected;
        m_lastDeviceChangeLogGameMode = m_gameModeActive;
    }

    if (!m_connected) {
        m_outputBackendMissing = false;
        ReleasePotentialShellKeys();
        logging::Logf("[ControllerManager] Released potential native shell keys on device-change while disconnected");
        TryOpen();
    }
    else if (g_ctrl && !g_ctrl->IsOpen())
        Close(/*restoreLizard=*/false);
}

void ControllerManager::OnSuspend() {
    logging::Logf("[ControllerManager] OnSuspend connected=%d gameMode=%d",
                  m_connected ? 1 : 0, m_gameModeActive ? 1 : 0);
    Close(/*restoreLizard=*/false);
}

void ControllerManager::OnResume() {
    logging::Logf("[ControllerManager] OnResume");
    m_outputBackendMissing = false;
    Close(/*restoreLizard=*/false);
    TryOpen();
}

void ControllerManager::EnableGameMode() {
    logging::Logf("[ControllerManager] EnableGameMode connected=%d active=%d",
                  m_connected ? 1 : 0, m_gameModeActive ? 1 : 0);
    if (!m_connected || m_gameModeActive) return;
    m_outputBackendMissing = false;
    g_ctrl->SetDesktopTrackpadMouseMode(
        kEnableFirmwareTrackpadMouse && m_trackpadMouseEnabled,
        m_useLeftTrackpad);
    if (!g_ctrl->DisableLizardMode()) {
        logging::Logf("[ControllerManager] EnableGameMode failed at DisableLizardMode");
        return;
    }

    SteamController* ctrl = g_ctrl.get();
    m_virtual = std::make_unique<VirtualController>(
        m_emulationMode,
        m_paddleMappings,
        m_paddleActions,
        [ctrl](uint8_t largeMotor, uint8_t smallMotor) {
            if (ctrl)
                ctrl->SetRumble(largeMotor, smallMotor);
        });
    if (!m_virtual->IsValid()) {
        bool missing = m_virtual->IsDriverMissing();
        m_outputBackendMissing = missing;
        logging::Logf("[ControllerManager] EnableGameMode failed at VirtualController valid=0 missing=%d",
                      missing ? 1 : 0);
        m_virtual.reset();
        g_ctrl->EnableLizardMode();
        if (missing) m_onStateChanged(m_connected, m_gameModeActive, /*outputBackendMissing=*/true);
        return;
    }

    m_gameModeActive = true;
    ReleasePotentialShellKeys();
    logging::Logf("[ControllerManager] Released potential native shell keys after lizard-mode transition");
    m_trackpad.Reset();
    m_trackpad.SetTrackpadEnabled(m_trackpadMouseEnabled);
    m_trackpad.SetBackButtonsEnabled(m_backButtonsEnabled);
    m_trackpad.SetUseLeftTrackpad(m_useLeftTrackpad);
    m_trackpad.SetHapticCallback([this]() { PulseTrackpadClickHaptics(); });
    m_trackpad.SetMouseUpdateCallback([this](int16_t dx, int16_t dy, uint8_t buttons) {
        if (m_virtual) m_virtual->UpdateMouse(dx, dy, buttons);
    });
    m_paddleOverlay.SetKeyChordCallback([this](const std::vector<uint16_t>& chord, bool down) {
        if (m_virtual) {
            if (down) m_virtual->KeyChordDown(chord);
            else      m_virtual->KeyChordUp(chord);
        }
    });
    m_trackpad.SetFirmwareMouseEnabled(kEnableFirmwareTrackpadMouse && m_trackpadMouseEnabled);
    m_paddleOverlay.SetBindings(m_paddleActions);
    StartReadLoop();
    logging::Logf("[ControllerManager] EnableGameMode success");
    m_onStateChanged(m_connected, m_gameModeActive, false);
}

void ControllerManager::DisableGameMode() {
    logging::Logf("[ControllerManager] DisableGameMode active=%d", m_gameModeActive ? 1 : 0);
    if (!m_gameModeActive) return;
    StopReadLoop();
    m_trackpad.Reset();
    m_paddleOverlay.Reset();
    m_virtual.reset();
    g_ctrl->EnableLizardMode();
    m_gameModeActive = false;
    m_onStateChanged(m_connected, m_gameModeActive, false);
}

void ControllerManager::DetachVirtual() {
    if (!m_gameModeActive || !m_virtual) return;
    logging::Logf("[ControllerManager] DetachVirtual");
    m_virtual.reset();
}

void ControllerManager::AttachVirtual() {
    if (!m_gameModeActive || m_virtual) return;
    logging::Logf("[ControllerManager] AttachVirtual");
    SteamController* ctrl = g_ctrl.get();
    m_virtual = std::make_unique<VirtualController>(
        m_emulationMode,
        m_paddleMappings,
        m_paddleActions,
        [ctrl](uint8_t largeMotor, uint8_t smallMotor) {
            if (ctrl) ctrl->SetRumble(largeMotor, smallMotor);
        });
    if (!m_virtual->IsValid()) {
        logging::Logf("[ControllerManager] AttachVirtual failed valid=0 missing=%d",
                      m_virtual->IsDriverMissing() ? 1 : 0);
        m_virtual.reset();
    }
}

void ControllerManager::SetTrackpadMouseEnabled(bool enabled) {
    m_trackpadMouseEnabled = enabled;
    m_trackpad.SetTrackpadEnabled(enabled);
    m_trackpad.SetFirmwareMouseEnabled(kEnableFirmwareTrackpadMouse && enabled);
    if (g_ctrl)
        g_ctrl->SetDesktopTrackpadMouseMode(kEnableFirmwareTrackpadMouse && m_trackpadMouseEnabled, m_useLeftTrackpad);
}

void ControllerManager::SetBackButtonsEnabled(bool enabled) {
    m_backButtonsEnabled = enabled;
    m_trackpad.SetBackButtonsEnabled(enabled);
}

void ControllerManager::SetUseLeftTrackpad(bool enabled) {
    m_useLeftTrackpad = enabled;
    m_trackpad.SetUseLeftTrackpad(enabled);
    if (g_ctrl)
        g_ctrl->SetDesktopTrackpadMouseMode(kEnableFirmwareTrackpadMouse && m_trackpadMouseEnabled, m_useLeftTrackpad);
}

void ControllerManager::SetEmulationMode(EmulationMode mode) {
    if (m_emulationMode == mode)
        return;

    const bool wasActive = m_gameModeActive;
    logging::Logf("[ControllerManager] SetEmulationMode old=%d new=%d active=%d",
                  static_cast<int>(m_emulationMode),
                  static_cast<int>(mode),
                  wasActive ? 1 : 0);
    m_emulationMode = mode;

    if (wasActive) {
        DisableGameMode();
        EnableGameMode();
    }
}

void ControllerManager::SetPaddleMapping(int paddleIndex, PaddleMapping mapping) {
    PaddleMapping* slot = nullptr;
    switch (paddleIndex) {
    case 0: slot = &m_paddleMappings.l4; break;
    case 1: slot = &m_paddleMappings.l5; break;
    case 2: slot = &m_paddleMappings.r4; break;
    case 3: slot = &m_paddleMappings.r5; break;
    case 4: slot = &m_paddleMappings.qam; break;
    default: return;
    }

    if (*slot == mapping)
        return;

    *slot = mapping;
    logging::Logf("[ControllerManager] SetPaddleMapping paddle=%d mapping=%d",
                  paddleIndex, static_cast<int>(mapping));

    if (m_virtual)
        m_virtual->SetPaddleMappings(m_paddleMappings);
}

void ControllerManager::SetPaddleActions(PaddleActionBindings actions) {
    m_paddleActions = std::move(actions);
    m_paddleOverlay.SetBindings(m_paddleActions);
    if (m_virtual)
        m_virtual->SetPaddleActions(m_paddleActions);
}

void ControllerManager::TryOpen() {
    logging::Logf("[ControllerManager] TryOpen");
    if (!g_ctrl) g_ctrl = std::make_unique<SteamController>();
    if (g_ctrl->Open()) {
        m_connected = true;
        m_outputBackendMissing = false;
        logging::Logf("[ControllerManager] TryOpen success");
        m_onStateChanged(m_connected, m_gameModeActive, false);
    } else {
        logging::Logf("[ControllerManager] TryOpen failed");
    }
}

void ControllerManager::Close(bool restoreLizard) {
    StopReadLoop();
    m_virtual.reset();
    m_paddleOverlay.Reset();
    m_trackpad.SetHapticCallback({});
    m_trackpad.SetMouseUpdateCallback({});
    m_paddleOverlay.SetKeyChordCallback({});
    if (g_ctrl) {
        if (restoreLizard && m_gameModeActive)
            g_ctrl->EnableLizardMode();
        g_ctrl->Close();
    }
    m_connected      = false;
    m_gameModeActive = false;
    m_outputBackendMissing = false;
    m_onStateChanged(m_connected, m_gameModeActive, false);
}

void ControllerManager::StartReadLoop() {
    m_readRunning = true;
    m_readThread  = std::thread(&ControllerManager::ReadLoop, this);
}

void ControllerManager::StopReadLoop() {
    m_readRunning = false;
    if (m_readThread.joinable())
        m_readThread.join();
}

void ControllerManager::ReadLoop() {
    uint8_t buf[64];
    while (m_readRunning) {
        size_t n = g_ctrl->ReadReport(buf, sizeof(buf), /*timeoutMs=*/32);
        if (n == 0) continue;
        if (!SteamController::IsStateLikeReport(buf, n)) continue;
        StandardGamepadState standardState;
        m_sdlInput.Poll(standardState);
        {
            std::lock_guard<std::mutex> lock(m_lastReportMutex);
            m_lastReportSize = (std::min)(n, m_lastReport.size());
            memcpy(m_lastReport.data(), buf, m_lastReportSize);
        }
        {
            std::lock_guard<std::mutex> lock(m_standardStateMutex);
            m_lastStandardState = standardState;
        }
        if (m_virtual) m_virtual->Update(buf, n, &standardState);
        m_paddleOverlay.Update(buf, n, &standardState);
        m_trackpad.Update(buf, n, &standardState);
    }
}

void ControllerManager::PulseTrackpadClickHaptics() {
    if (!g_ctrl || !m_gameModeActive)
        return;
    const std::uint64_t now = GetTickCount64();
    const std::uint64_t last = m_lastTrackpadHapticPulseTickMs.load();
    if (now - last < 40)
        return;
    m_lastTrackpadHapticPulseTickMs = now;
    g_ctrl->PulseHaptic(/*strength=*/64);
}

StandardGamepadState ControllerManager::GetLatestStandardState() const {
    std::lock_guard<std::mutex> lock(m_standardStateMutex);
    return m_lastStandardState;
}

std::wstring ControllerManager::GetCurrentMacroCaptureChord() const {
    std::array<uint8_t, 64> buf{};
    size_t n = 0;
    {
        std::lock_guard<std::mutex> lock(m_lastReportMutex);
        n = m_lastReportSize;
        if (n == 0)
            return {};
        memcpy(buf.data(), m_lastReport.data(), n);
    }

    if (n < 18 || !SteamController::UsesLegacyStateLayout(buf.data(), n))
        return {};

    std::wstring chord;
    const StandardGamepadState standard = GetLatestStandardState();
    if (standard.connected) {
        AppendStandardChordTokens(chord, standard);
    } else {
        const uint8_t b0 = buf[2];
        const uint8_t b1 = buf[3];
        const uint8_t b2 = buf[4];
        int16_t ltRaw = 0;
        int16_t rtRaw = 0;
        memcpy(&ltRaw, buf.data() + 6, 2);
        memcpy(&rtRaw, buf.data() + 8, 2);

        if (b0 & SteamController::BTN_A) AppendChordToken(chord, L"A");
        if (b0 & SteamController::BTN_B) AppendChordToken(chord, L"B");
        if (b0 & SteamController::BTN_X) AppendChordToken(chord, L"X");
        if (b0 & SteamController::BTN_Y) AppendChordToken(chord, L"Y");
        if (b2 & SteamController::BTN_LB) AppendChordToken(chord, L"LB");
        if (b1 & SteamController::BTN_RB) AppendChordToken(chord, L"RB");
        if (ltRaw > 2048) AppendChordToken(chord, L"LT");
        if (rtRaw > 2048) AppendChordToken(chord, L"RT");
        if (b0 & SteamController::BTN_MENU) AppendChordToken(chord, L"MENU");
        if (b1 & SteamController::BTN_VIEW) AppendChordToken(chord, L"VIEW");
        if (b2 & SteamController::BTN_STEAM) AppendChordToken(chord, L"GUIDE");
        if (b1 & SteamController::BTN_LS) AppendChordToken(chord, L"L3");
        if (b0 & SteamController::BTN_RS) AppendChordToken(chord, L"R3");
        if (b1 & SteamController::BTN_DPAD_UP) AppendChordToken(chord, L"DPADUP");
        if (b1 & SteamController::BTN_DPAD_RT) AppendChordToken(chord, L"DPADRIGHT");
        if (b1 & SteamController::BTN_DPAD_DN) AppendChordToken(chord, L"DPADDOWN");
        if (b1 & SteamController::BTN_DPAD_LT) AppendChordToken(chord, L"DPADLEFT");
    }
    const uint8_t b0 = buf[2];
    const uint8_t b1 = buf[3];
    const uint8_t b2 = buf[4];
    if (b2 & SteamController::BTN_L4) AppendChordToken(chord, L"L4");
    if (b2 & SteamController::BTN_L5) AppendChordToken(chord, L"L5");
    if (b0 & SteamController::BTN_R4) AppendChordToken(chord, L"R4");
    if (b1 & SteamController::BTN_R5) AppendChordToken(chord, L"R5");
    if (b0 & SteamController::BTN_QAM) AppendChordToken(chord, L"QAM");
    return chord;
}

ControllerManager::UiNavigationState ControllerManager::GetUiNavigationState() const {
    std::array<uint8_t, 64> buf{};
    size_t n = 0;
    {
        std::lock_guard<std::mutex> lock(m_lastReportMutex);
        n = m_lastReportSize;
        if (n == 0)
            return {};
        memcpy(buf.data(), m_lastReport.data(), n);
    }

    if (n < 18 || !SteamController::UsesLegacyStateLayout(buf.data(), n))
        return {};

    UiNavigationState state;
    const StandardGamepadState standard = GetLatestStandardState();
    if (standard.connected) {
        state.confirm = standard.a;
        state.back = standard.b || standard.back;
        state.clear = standard.x;
        state.record = standard.y;
        state.previous = standard.leftShoulder;
        state.next = standard.rightShoulder;
        state.up = standard.dpadUp;
        state.right = standard.dpadRight;
        state.down = standard.dpadDown;
        state.left = standard.dpadLeft;
    } else {
        const uint8_t b0 = buf[2];
        const uint8_t b1 = buf[3];
        const uint8_t b2 = buf[4];
        state.confirm = (b0 & SteamController::BTN_A) != 0;
        state.back = ((b0 & SteamController::BTN_B) != 0) || ((b1 & SteamController::BTN_VIEW) != 0);
        state.clear = (b0 & SteamController::BTN_X) != 0;
        state.record = (b0 & SteamController::BTN_Y) != 0;
        state.previous = (b2 & SteamController::BTN_LB) != 0;
        state.next = (b1 & SteamController::BTN_RB) != 0;
        state.up = (b1 & SteamController::BTN_DPAD_UP) != 0;
        state.right = (b1 & SteamController::BTN_DPAD_RT) != 0;
        state.down = (b1 & SteamController::BTN_DPAD_DN) != 0;
        state.left = (b1 & SteamController::BTN_DPAD_LT) != 0;
    }
    return state;
}

std::wstring ControllerManager::GetControllerReportSignature() const {
    if (!g_ctrl)
        return {};
    return g_ctrl->GetLastReportSignature();
}
