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
}

static std::unique_ptr<SteamController> g_ctrl;

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
    logging::Logf("[ControllerManager] OnDeviceChange connected=%d gameMode=%d",
                  m_connected ? 1 : 0, m_gameModeActive ? 1 : 0);
    if (!m_connected)
        TryOpen();
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
    Close(/*restoreLizard=*/false);
    TryOpen();
}

void ControllerManager::EnableGameMode() {
    logging::Logf("[ControllerManager] EnableGameMode connected=%d active=%d",
                  m_connected ? 1 : 0, m_gameModeActive ? 1 : 0);
    if (!m_connected || m_gameModeActive) return;
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
        logging::Logf("[ControllerManager] EnableGameMode failed at VirtualController valid=0 missing=%d",
                      missing ? 1 : 0);
        m_virtual.reset();
        g_ctrl->EnableLizardMode();
        if (missing) m_onStateChanged(m_connected, m_gameModeActive, /*vigemMissing=*/true);
        return;
    }

    m_gameModeActive = true;
    m_trackpad.Reset();
    m_trackpad.SetTrackpadEnabled(m_trackpadMouseEnabled);
    m_trackpad.SetBackButtonsEnabled(m_backButtonsEnabled);
    m_trackpad.SetUseLeftTrackpad(m_useLeftTrackpad);
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

void ControllerManager::SetTrackpadMouseEnabled(bool enabled) {
    m_trackpadMouseEnabled = enabled;
    m_trackpad.SetTrackpadEnabled(enabled);
}

void ControllerManager::SetBackButtonsEnabled(bool enabled) {
    m_backButtonsEnabled = enabled;
    m_trackpad.SetBackButtonsEnabled(enabled);
}

void ControllerManager::SetUseLeftTrackpad(bool enabled) {
    m_useLeftTrackpad = enabled;
    m_trackpad.SetUseLeftTrackpad(enabled);
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
    if (g_ctrl) {
        if (restoreLizard && m_gameModeActive)
            g_ctrl->EnableLizardMode();
        g_ctrl->Close();
    }
    m_connected      = false;
    m_gameModeActive = false;
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
        if (buf[0] != SteamController::REPORT_STATE) continue;
        {
            std::lock_guard<std::mutex> lock(m_lastReportMutex);
            m_lastReportSize = (std::min)(n, m_lastReport.size());
            memcpy(m_lastReport.data(), buf, m_lastReportSize);
        }
        if (m_virtual) m_virtual->Update(buf, n);
        m_paddleOverlay.Update(buf, n);
        m_trackpad.Update(buf, n);
    }
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

    if (n < 18 || buf[0] != SteamController::REPORT_STATE)
        return {};

    const uint8_t b0 = buf[2];
    const uint8_t b1 = buf[3];
    const uint8_t b2 = buf[4];
    int16_t ltRaw = 0;
    int16_t rtRaw = 0;
    memcpy(&ltRaw, buf.data() + 6, 2);
    memcpy(&rtRaw, buf.data() + 8, 2);

    std::wstring chord;
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

    if (n < 18 || buf[0] != SteamController::REPORT_STATE)
        return {};

    const uint8_t b0 = buf[2];
    const uint8_t b1 = buf[3];
    const uint8_t b2 = buf[4];

    UiNavigationState state;
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
    return state;
}
