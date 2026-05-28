#pragma once
#include "PaddleOverlay.h"
#include "SdlGamepadInput.h"
#include "TrackpadMouse.h"
#include "VirtualController.h"
#include <functional>
#include <thread>
#include <atomic>
#include <memory>
#include <cstdint>
#include <array>
#include <mutex>
#include <string>
#include <atomic>

// Manages the Steam Controller lifecycle: device discovery, lizard mode
// disable/enable, and the heartbeat that keeps lizard mode off.
// All public methods are safe to call from the UI thread.
class ControllerManager {
public:
    struct UiNavigationState {
        bool up = false;
        bool down = false;
        bool left = false;
        bool right = false;
        bool confirm = false;
        bool back = false;
        bool previous = false;
        bool next = false;
        bool clear = false;
        bool record = false;
    };

    using StateChangedFn = std::function<void(bool connected, bool gameModeActive, bool outputBackendMissing)>;

    explicit ControllerManager(StateChangedFn onStateChanged);
    ~ControllerManager();
    ControllerManager(const ControllerManager&) = delete;
    ControllerManager& operator=(const ControllerManager&) = delete;

    // Called when Windows reports a device arrival or removal (WM_DEVICECHANGE).
    void OnDeviceChange();
    void OnSuspend();
    void OnResume();
    void RecoverIfInputStalled();

    // Toggle game mode on/off. No-op if controller is not connected.
    void EnableGameMode();
    void DisableGameMode();

    void SetTrackpadMouseEnabled(bool enabled);
    void SetBackButtonsEnabled(bool enabled);
    void SetUseLeftTrackpad(bool enabled);
    void SetEmulationMode(EmulationMode mode);
    void SetPaddleMapping(int paddleIndex, PaddleMapping mapping);
    void SetPaddleActions(PaddleActionBindings actions);

    bool IsConnected()             const { return m_connected; }
    bool IsGameModeActive()        const { return m_gameModeActive; }
    bool IsOutputBackendMissing()  const { return m_outputBackendMissing; }
    bool IsTrackpadMouseEnabled()  const { return m_trackpadMouseEnabled; }
    bool IsBackButtonsEnabled()    const { return m_backButtonsEnabled; }
    bool IsUseLeftTrackpad()       const { return m_useLeftTrackpad; }
    EmulationMode GetEmulationMode() const { return m_emulationMode; }
    PaddleMappings GetPaddleMappings() const { return m_paddleMappings; }
    PaddleActionBindings GetPaddleActions() const { return m_paddleActions; }
    std::wstring GetCurrentMacroCaptureChord() const;
    UiNavigationState GetUiNavigationState() const;
    std::wstring GetControllerReportSignature() const;

private:
    StandardGamepadState GetLatestStandardState() const;
    void TryOpen();
    void Close(bool restoreLizard);
    void StartReadLoop();
    void StopReadLoop();
    void ReadLoop();
    void PulseTrackpadClickHaptics();

    StateChangedFn                     m_onStateChanged;
    bool                               m_connected            = false;
    bool                               m_gameModeActive       = false;
    bool                               m_outputBackendMissing = false;
    bool                               m_trackpadMouseEnabled = true;
    bool                               m_backButtonsEnabled   = false;
    bool                               m_useLeftTrackpad      = false;
    EmulationMode                      m_emulationMode        = EmulationMode::Xbox360;
    PaddleMappings                     m_paddleMappings{};
    PaddleActionBindings               m_paddleActions{};
    std::unique_ptr<VirtualController> m_virtual;
    PaddleOverlay                      m_paddleOverlay;
    TrackpadMouse                      m_trackpad;
    std::thread                        m_readThread;
    std::atomic<bool>                  m_readRunning{false};
    std::atomic<std::uint64_t>         m_lastReportTickMs{0};
    std::atomic<std::uint64_t>         m_lastTrackpadHapticPulseTickMs{0};
    std::uint64_t                      m_lastDeviceChangeLogTickMs = 0;
    bool                               m_lastDeviceChangeLogConnected = false;
    bool                               m_lastDeviceChangeLogGameMode = false;
    mutable std::mutex                 m_lastReportMutex;
    std::array<uint8_t, 64>            m_lastReport{};
    size_t                             m_lastReportSize = 0;
    mutable std::mutex                 m_standardStateMutex;
    StandardGamepadState               m_lastStandardState{};
    SdlGamepadInput                    m_sdlInput;
};
