#pragma once
#include "ControllerManager.h"
#include "RemapBackend.h"
#include "VirtualController.h"
#include <Windows.h>
#include <commctrl.h>
#include <array>
#include <functional>
#include <string>
#include <vector>

class PaddleConfigWindow {
public:
    using ControllerChordFn = std::function<std::wstring()>;
    using ControllerUiState = ControllerManager::UiNavigationState;
    using ControllerUiStateFn = std::function<ControllerUiState()>;
    using LoadAutoSwitchFn = std::function<bool()>;
    using SaveAutoSwitchFn = std::function<void(bool)>;
    using ApplyProfileFn = std::function<void(const std::wstring&, bool force)>;

    PaddleConfigWindow(RemapBackend& backend,
                       ControllerChordFn controllerChordFn,
                       ControllerUiStateFn controllerUiStateFn,
                       LoadAutoSwitchFn loadAutoSwitch,
                       SaveAutoSwitchFn saveAutoSwitch,
                       ApplyProfileFn onApplyProfile);
    ~PaddleConfigWindow() = default;

    void Show(HINSTANCE hInstance, HWND owner);
    void Close();
    void ReloadFromModel();
    bool IsOpen() const { return m_hwnd != nullptr; }
    HWND GetHwnd() const { return m_hwnd; }

private:
    static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);

    LRESULT HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp);
    void CreateControls();
    void RefreshFromModel();
    void RefreshProfileState();
    void RefreshBindingSummary();
    void RefreshEditorForSelectedPaddle();
    void RefreshControllerFocus();
    void UpdateControlState();
    void RecordMacro();
    void CommitPendingChanges();
    void PersistCurrentState();
    void ApplySelection();
    void UseSelectedGameProfile();
    void RefreshInstalledGames(bool forceRefresh = false);
    void AddGameFolderSource();
    void AddGameExeSource();
    void RemoveSelectedGameSource();
    void RefreshGameSourcesUi();
    void SetRefreshUiState(bool refreshing, const wchar_t* statusText = nullptr);
    int CurrentModeSelection() const;
    void SetModeSelectionForCurrent(int modeIndex);
    void HandleControllerTimer();
    void MoveControllerFocus(int delta);
    void CycleCurrentProfile(int delta);
    void CycleCurrentPaddle(int delta);
    void CycleCurrentMode(int delta);
    void CycleCurrentGamepad(int delta);
    void CycleGameSourceSelection(int delta);
    void ToggleAutoSwitch();
    void ActivateFocusedControl();
    bool FocusControllerControl(HWND target);
    HWND ControllerFocusHwnd() const;
    void InvalidateFocusOutline(HWND control);
    void InvalidatePreviewArea();
    HWND FocusedComboForController() const;
    bool IsControllerComboDropped(HWND combo) const;
    void ToggleControllerComboDropdown(HWND combo);
    void StepControllerComboSelection(HWND combo, int delta);
    void CommitControllerComboSelection(HWND combo);
    void Paint(HDC hdc);
    RECT PaddleRect(int paddleIndex) const;
    POINT PaddleAnchor(int paddleIndex) const;
    PaddleAction* SelectedAction();
    PaddleMapping* SelectedMapping();
    std::wstring PaddleLabelText(int paddleIndex) const;
    int HitTestPaddleLabel(POINT pt) const;

    HWND m_hwnd = nullptr;
    HWND m_owner = nullptr;
    HINSTANCE m_hInstance = nullptr;

    HWND m_comboMode = nullptr;
    HWND m_comboGamepad = nullptr;
    HWND m_editBinding = nullptr;
    HWND m_staticBinding = nullptr;
    HWND m_staticBindingHelp = nullptr;
    HWND m_staticSelected = nullptr;
    HWND m_staticProfile = nullptr;
    HWND m_comboPaddleSelect = nullptr;
    HWND m_staticBindingSummary = nullptr;
    HWND m_comboGameProfiles = nullptr;
    HWND m_buttonRefreshLibrary = nullptr;
    HWND m_listGameSources = nullptr;
    HWND m_buttonAddFolder = nullptr;
    HWND m_buttonAddExe = nullptr;
    HWND m_buttonRemoveSource = nullptr;
    HWND m_staticRefreshStatus = nullptr;
    HWND m_progressRefresh = nullptr;
    HWND m_checkAutoSwitch = nullptr;
    HWND m_checkRapid = nullptr;
    HWND m_buttonRecord = nullptr;
    HWND m_hoverLabelPopup = nullptr;
    HWND m_tooltip = nullptr;

    RemapBackend& m_backend;
    ControllerChordFn m_controllerChordFn;
    ControllerUiStateFn m_controllerUiStateFn;
    LoadAutoSwitchFn m_loadAutoSwitch;
    SaveAutoSwitchFn m_saveAutoSwitch;
    ApplyProfileFn m_onApplyProfile;

    PaddleMappings       m_mappings{};
    PaddleActionBindings m_actions{};
    int                  m_selectedPaddle = 0;
    int                  m_hoveredTooltipPaddle = -1;
    bool                 m_updatingControls = false;
    bool                 m_autoSwitchProfiles = false;
    int                  m_controllerFocusIndex = 0;
    std::array<int, 5>   m_modeSelections = { 0, 0, 0, 0, 0 };
    ControllerUiState    m_lastControllerUiState{};
    std::wstring         m_tooltipText;
    std::wstring         m_editProfileId = L"default";
    std::vector<std::wstring> m_gameSourceSpecs;
    std::vector<std::wstring> m_installedGames;
};
