#include "PaddleConfigWindow.h"
#include "MacroRecorder.h"
#include "PaddleConfig.h"
#include "logging/Log.h"
#include "resource.h"
#include <ShlObj.h>
#include <commdlg.h>
#include <algorithm>
#include <gdiplus.h>
#include <memory>
#include <windowsx.h>
#include <string>

namespace {
constexpr wchar_t kClassName[] = L"SteamControllerRemapperPaddleConfig";
constexpr int IDC_MODE = 2001;
constexpr int IDC_GAMEPAD = 2002;
constexpr int IDC_BINDING = 2003;
constexpr int IDC_CLOSE = 2005;
constexpr int IDC_SELECTED = 2006;
constexpr int IDC_RAPID = 2008;
constexpr int IDC_RECORD = 2009;
constexpr int IDC_PROFILE_CURRENT = 2011;
constexpr int IDC_PADDLE_CURRENT = 2012;
constexpr int IDC_LIBRARY_REFRESH = 2013;
constexpr int IDC_GAME_LOCATION = 2014;
constexpr int IDC_GAME_SOURCE_LIST = 2015;
constexpr int IDC_GAME_SOURCE_EXE = 2016;
constexpr int IDC_GAME_SOURCE_REMOVE = 2017;
constexpr int IDC_AUTO_SWITCH = 2018;
constexpr UINT_PTR UI_NAV_TIMER_ID = 1;
constexpr UINT UI_NAV_TIMER_MS = 90;
constexpr UINT_PTR TOOLTIP_BASE_ID = 5000;
constexpr int kButtonCount = 5;
constexpr int kDefaultControllerFocusIndex = 7;

ULONG_PTR EnsureGdiplus() {
    static ULONG_PTR token = 0;
    static bool initialized = false;
    if (!initialized) {
        Gdiplus::GdiplusStartupInput input;
        Gdiplus::GdiplusStartup(&token, &input, nullptr);
        initialized = true;
    }
    return token;
}

Gdiplus::Image* LoadControllerImage() {
    EnsureGdiplus();
    HRSRC resource = FindResourceW(nullptr, MAKEINTRESOURCEW(IDR_CONTROLLER_IMAGE), reinterpret_cast<LPCWSTR>(RT_RCDATA));
    if (!resource)
        return nullptr;
    const DWORD size = SizeofResource(nullptr, resource);
    HGLOBAL handle = LoadResource(nullptr, resource);
    if (!handle)
        return nullptr;
    void* data = LockResource(handle);
    if (!data)
        return nullptr;

    HGLOBAL buffer = GlobalAlloc(GMEM_MOVEABLE, size);
    if (!buffer)
        return nullptr;
    void* dest = GlobalLock(buffer);
    memcpy(dest, data, size);
    GlobalUnlock(buffer);

    IStream* stream = nullptr;
    if (CreateStreamOnHGlobal(buffer, TRUE, &stream) != S_OK) {
        GlobalFree(buffer);
        return nullptr;
    }

    Gdiplus::Image* image = Gdiplus::Image::FromStream(stream);
    stream->Release();
    return image;
}

const wchar_t* PaddleName(int index) {
    switch (index) {
    case 0: return L"L4";
    case 1: return L"L5";
    case 2: return L"R4";
    case 3: return L"R5";
    default: return L"QAM";
    }
}

const PaddleMapping kGamepadOptions[] = {
    PaddleMapping::None,
    PaddleMapping::A,
    PaddleMapping::B,
    PaddleMapping::X,
    PaddleMapping::Y,
    PaddleMapping::LeftShoulder,
    PaddleMapping::RightShoulder,
    PaddleMapping::View,
    PaddleMapping::Menu,
    PaddleMapping::LeftThumb,
    PaddleMapping::RightThumb,
    PaddleMapping::Guide,
    PaddleMapping::DPadUp,
    PaddleMapping::DPadRight,
    PaddleMapping::DPadDown,
    PaddleMapping::DPadLeft,
};

const wchar_t* GamepadName(PaddleMapping mapping) {
    switch (mapping) {
    case PaddleMapping::None: return L"Unmapped";
    case PaddleMapping::A: return L"A / Cross";
    case PaddleMapping::B: return L"B / Circle";
    case PaddleMapping::X: return L"X / Square";
    case PaddleMapping::Y: return L"Y / Triangle";
    case PaddleMapping::LeftShoulder: return L"Left Shoulder";
    case PaddleMapping::RightShoulder: return L"Right Shoulder";
    case PaddleMapping::View: return L"View / Share";
    case PaddleMapping::Menu: return L"Menu / Options";
    case PaddleMapping::LeftThumb: return L"Left Stick Click";
    case PaddleMapping::RightThumb: return L"Right Stick Click";
    case PaddleMapping::Guide: return L"Guide / PS";
    case PaddleMapping::DPadUp: return L"D-Pad Up";
    case PaddleMapping::DPadRight: return L"D-Pad Right";
    case PaddleMapping::DPadDown: return L"D-Pad Down";
    case PaddleMapping::DPadLeft: return L"D-Pad Left";
    }
    return L"Unmapped";
}

std::wstring ActionTextForEditor(const PaddleAction& action, PaddleMapping fallback) {
    if (action.type == PaddleActionType::Macro) {
        std::wstring text = PaddleConfig::Describe(action, fallback);
        if (text.rfind(L"Macro: ", 0) == 0)
            text.erase(0, 7);
        const size_t suffix = text.find(L" [");
        if (suffix != std::wstring::npos)
            text.erase(suffix);
        return text;
    }
    if (action.type == PaddleActionType::KeyChord) {
        std::wstring text = PaddleConfig::Describe(action, fallback);
        const size_t suffix = text.find(L" [");
        if (suffix != std::wstring::npos)
            text.erase(suffix);
        return text;
    }
    return L"";
}

std::wstring GameSourceDisplayText(const std::wstring& spec) {
    if (spec.rfind(L"DIR|", 0) == 0)
        return L"[Folder] " + spec.substr(4);
    if (spec.rfind(L"EXE|", 0) == 0)
        return L"[EXE] " + spec.substr(4);
    return spec;
}
}

PaddleConfigWindow::PaddleConfigWindow(RemapBackend& backend,
                                       ControllerChordFn controllerChordFn,
                                       ControllerUiStateFn controllerUiStateFn,
                                       LoadAutoSwitchFn loadAutoSwitch,
                                       SaveAutoSwitchFn saveAutoSwitch,
                                       ApplyProfileFn onApplyProfile)
    : m_backend(backend),
      m_controllerChordFn(std::move(controllerChordFn)),
      m_controllerUiStateFn(std::move(controllerUiStateFn)),
      m_loadAutoSwitch(std::move(loadAutoSwitch)),
      m_saveAutoSwitch(std::move(saveAutoSwitch)),
      m_onApplyProfile(std::move(onApplyProfile)) {
    m_controllerFocusIndex = kDefaultControllerFocusIndex;
}

void PaddleConfigWindow::Show(HINSTANCE hInstance, HWND owner) {
    m_owner = owner;
    m_hInstance = hInstance;

    if (m_hwnd) {
        ShowWindow(m_hwnd, SW_SHOWNORMAL);
        SetForegroundWindow(m_hwnd);
        RefreshFromModel();
        return;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    m_hwnd = CreateWindowExW(
        WS_EX_APPWINDOW,
        kClassName,
        L"Remap Buttons",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 920, 780,
        owner, nullptr, hInstance, this);

    CreateControls();
    RefreshFromModel();
    ShowWindow(m_hwnd, SW_SHOWNORMAL);
    SetWindowPos(m_hwnd, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetWindowPos(m_hwnd, HWND_NOTOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
    SetForegroundWindow(m_hwnd);
    RefreshControllerFocus();
    SetTimer(m_hwnd, UI_NAV_TIMER_ID, UI_NAV_TIMER_MS, nullptr);
    UpdateWindow(m_hwnd);
}

void PaddleConfigWindow::Close() {
    if (m_hwnd)
        DestroyWindow(m_hwnd);
}

void PaddleConfigWindow::ReloadFromModel() {
    if (m_hwnd)
        RefreshFromModel();
}

LRESULT CALLBACK PaddleConfigWindow::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    PaddleConfigWindow* self = reinterpret_cast<PaddleConfigWindow*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        const CREATESTRUCTW* create = reinterpret_cast<const CREATESTRUCTW*>(lp);
        self = reinterpret_cast<PaddleConfigWindow*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(self));
    }

    if (self)
        return self->HandleMessage(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT PaddleConfigWindow::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_CLOSE:
            CommitPendingChanges();
            DestroyWindow(hwnd);
            return 0;
        case IDC_RECORD:
            RecordMacro();
            return 0;
        case IDC_PROFILE_CURRENT:
            if (HIWORD(wp) == CBN_SELCHANGE && !m_updatingControls)
                UseSelectedGameProfile();
            return 0;
        case IDC_PADDLE_CURRENT:
            if (HIWORD(wp) == CBN_SELCHANGE && !m_updatingControls) {
                const int selectedIndex = static_cast<int>(SendMessageW(m_comboPaddleSelect, CB_GETCURSEL, 0, 0));
                if (selectedIndex >= 0 && selectedIndex < kButtonCount) {
                    m_selectedPaddle = selectedIndex;
                    RefreshEditorForSelectedPaddle();
                    InvalidateRect(hwnd, nullptr, TRUE);
                }
            }
            return 0;
        case IDC_LIBRARY_REFRESH:
            RefreshInstalledGames(true);
            RefreshProfileState();
            return 0;
        case IDC_GAME_LOCATION:
            AddGameFolderSource();
            return 0;
        case IDC_GAME_SOURCE_EXE:
            AddGameExeSource();
            return 0;
        case IDC_GAME_SOURCE_REMOVE:
            RemoveSelectedGameSource();
            return 0;
        case IDC_MODE:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                SetModeSelectionForCurrent(static_cast<int>(SendMessageW(m_comboMode, CB_GETCURSEL, 0, 0)));
                UpdateControlState();
                if (!m_updatingControls && CurrentModeSelection() != 1)
                    ApplySelection();
            }
            return 0;
        case IDC_GAMEPAD:
            if (HIWORD(wp) == CBN_SELCHANGE) {
                InvalidatePreviewArea();
                if (!m_updatingControls)
                    ApplySelection();
            }
            return 0;
        case IDC_RAPID:
            if (HIWORD(wp) == BN_CLICKED && !m_updatingControls)
                ApplySelection();
            return 0;
        case IDC_AUTO_SWITCH:
            if (HIWORD(wp) == BN_CLICKED && !m_updatingControls && m_saveAutoSwitch) {
                m_autoSwitchProfiles = Button_GetCheck(m_checkAutoSwitch) == BST_CHECKED;
                m_saveAutoSwitch(m_autoSwitchProfiles);
            }
            return 0;
        case IDC_BINDING:
            if (HIWORD(wp) == EN_KILLFOCUS && !m_updatingControls)
                ApplySelection();
            return 0;
        }
        break;
    case WM_TIMER:
        if (wp == UI_NAV_TIMER_ID) {
            HandleControllerTimer();
            return 0;
        }
        break;
    case WM_LBUTTONDOWN: {
        const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        for (int i = 0; i < kButtonCount; ++i) {
            RECT rect = PaddleRect(i);
            if (PtInRect(&rect, pt)) {
                m_selectedPaddle = i;
                RefreshEditorForSelectedPaddle();
                InvalidateRect(hwnd, nullptr, TRUE);
                return 0;
            }
        }
        break;
    }
    case WM_MOUSEMOVE: {
        TRACKMOUSEEVENT tme{ sizeof(tme), TME_LEAVE, m_hwnd, 0 };
        TrackMouseEvent(&tme);
        const POINT pt{ GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        const int hovered = HitTestPaddleLabel(pt);
        m_hoveredTooltipPaddle = hovered;
        if (hovered >= 0 && m_hoverLabelPopup) {
            m_tooltipText = PaddleLabelText(hovered);
            SetWindowTextW(m_hoverLabelPopup, m_tooltipText.c_str());

            HDC measureDc = GetDC(m_hoverLabelPopup);
            HGDIOBJ oldFont = SelectObject(measureDc, GetStockObject(DEFAULT_GUI_FONT));
            RECT textRect{ 0, 0, 0, 0 };
            DrawTextW(measureDc, m_tooltipText.c_str(), -1, &textRect,
                      DT_LEFT | DT_SINGLELINE | DT_NOPREFIX | DT_CALCRECT);
            SelectObject(measureDc, oldFont);
            ReleaseDC(m_hoverLabelPopup, measureDc);

            POINT screenPt = pt;
            ClientToScreen(m_hwnd, &screenPt);
            const int width = (textRect.right - textRect.left) + 16;
            const int height = (textRect.bottom - textRect.top) + 10;
            SetWindowPos(m_hoverLabelPopup, HWND_TOPMOST,
                         screenPt.x + 16, screenPt.y + 24, width, height,
                         SWP_NOACTIVATE | SWP_SHOWWINDOW);
        } else if (m_hoverLabelPopup) {
            ShowWindow(m_hoverLabelPopup, SW_HIDE);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        if (m_hoverLabelPopup)
            ShowWindow(m_hoverLabelPopup, SW_HIDE);
        m_hoveredTooltipPaddle = -1;
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        Paint(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        KillTimer(hwnd, UI_NAV_TIMER_ID);
        if (m_hoverLabelPopup) {
            DestroyWindow(m_hoverLabelPopup);
            m_hoverLabelPopup = nullptr;
        }
        m_hwnd = nullptr;
        return 0;
    case WM_SETFOCUS:
        RefreshControllerFocus();
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void PaddleConfigWindow::CreateControls() {
    CreateWindowW(L"STATIC", L"Editing profile:", WS_CHILD | WS_VISIBLE,
                  620, 18, 120, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    m_staticProfile = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                    740, 18, 150, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    CreateWindowW(L"STATIC", L"Installed game:", WS_CHILD | WS_VISIBLE,
                  620, 42, 120, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    m_comboGameProfiles = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | CBS_DROPDOWNLIST,
                                        620, 64, 140, 300, m_hwnd,
                                        static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_PROFILE_CURRENT))),
                                        m_hInstance, nullptr);
    SendMessageW(m_comboGameProfiles, CB_SETDROPPEDWIDTH, 260, 0);
    m_buttonRefreshLibrary = CreateWindowW(L"BUTTON", L"Refresh Library", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                           768, 64, 122, 24, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_LIBRARY_REFRESH))), m_hInstance, nullptr);
    CreateWindowW(L"STATIC", L"Game sources:", WS_CHILD | WS_VISIBLE,
                  620, 96, 120, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    m_listGameSources = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY,
                                      620, 118, 160, 84, m_hwnd,
                                      static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_GAME_SOURCE_LIST))),
                                      m_hInstance, nullptr);
    m_buttonAddFolder = CreateWindowW(L"BUTTON", L"Add Folder...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                      788, 118, 102, 24, m_hwnd,
                                      static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_GAME_LOCATION))),
                                      m_hInstance, nullptr);
    m_buttonAddExe = CreateWindowW(L"BUTTON", L"Add EXE...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                   788, 148, 102, 24, m_hwnd,
                                   static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_GAME_SOURCE_EXE))),
                                   m_hInstance, nullptr);
    m_buttonRemoveSource = CreateWindowW(L"BUTTON", L"Remove", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                         788, 178, 102, 24, m_hwnd,
                                         static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_GAME_SOURCE_REMOVE))),
                                         m_hInstance, nullptr);
    m_staticRefreshStatus = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                          620, 208, 160, 14, m_hwnd, nullptr, m_hInstance, nullptr);
    m_progressRefresh = CreateWindowExW(0, PROGRESS_CLASSW, nullptr, WS_CHILD | WS_VISIBLE | PBS_MARQUEE,
                                        620, 224, 160, 14, m_hwnd, nullptr, m_hInstance, nullptr);
    ShowWindow(m_progressRefresh, SW_HIDE);
    m_checkAutoSwitch = CreateWindowW(L"BUTTON", L"Auto-switch profiles when game launches",
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                      620, 248, 270, 22, m_hwnd,
                                      static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_AUTO_SWITCH))),
                                      m_hInstance, nullptr);

    CreateWindowW(L"STATIC", L"Button to edit:", WS_CHILD | WS_VISIBLE,
                  620, 282, 120, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    m_comboPaddleSelect = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                        620, 304, 240, 200, m_hwnd,
                                        static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_PADDLE_CURRENT))),
                                        m_hInstance, nullptr);
    for (int i = 0; i < kButtonCount; ++i)
        SendMessageW(m_comboPaddleSelect, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(PaddleName(i)));
    CreateWindowW(L"STATIC", L"Current binding:", WS_CHILD | WS_VISIBLE,
                  620, 336, 120, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    m_staticBindingSummary = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                           620, 356, 260, 28, m_hwnd, nullptr, m_hInstance, nullptr);
    m_staticSelected = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                     740, 282, 150, 20, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_SELECTED))), m_hInstance, nullptr);

    CreateWindowW(L"STATIC", L"Action type:", WS_CHILD | WS_VISIBLE,
                  620, 394, 120, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    m_comboMode = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                620, 416, 240, 220, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_MODE))), m_hInstance, nullptr);
    SendMessageW(m_comboMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Gamepad button"));
    SendMessageW(m_comboMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Shortcut / macro"));
    SendMessageW(m_comboMode, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Unmapped"));

    CreateWindowW(L"STATIC", L"Gamepad target:", WS_CHILD | WS_VISIBLE,
                  620, 448, 120, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    m_comboGamepad = CreateWindowW(L"COMBOBOX", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | CBS_DROPDOWNLIST,
                                   620, 470, 240, 300, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_GAMEPAD))), m_hInstance, nullptr);
    for (const PaddleMapping mapping : kGamepadOptions)
        SendMessageW(m_comboGamepad, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(GamepadName(mapping)));

    m_staticBinding = CreateWindowW(L"STATIC", L"Binding:", WS_CHILD | WS_VISIBLE,
                                    620, 532, 160, 20, m_hwnd, nullptr, m_hInstance, nullptr);
    m_editBinding = CreateWindowW(L"EDIT", L"", WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL,
                                  620, 554, 240, 24, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_BINDING))), m_hInstance, nullptr);
    m_staticBindingHelp = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                        620, 582, 240, 52, m_hwnd, nullptr, m_hInstance, nullptr);

    m_checkRapid = CreateWindowW(L"BUTTON", L"Rapid fire", WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_AUTOCHECKBOX,
                                 620, 502, 160, 22, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_RAPID))), m_hInstance, nullptr);
    m_buttonRecord = CreateWindowW(L"BUTTON", L"Capture Input...", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                   620, 644, 240, 28, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_RECORD))), m_hInstance, nullptr);
    CreateWindowW(L"BUTTON", L"Close", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                  620, 680, 240, 30, m_hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_CLOSE))), m_hInstance, nullptr);

    m_hoverLabelPopup = CreateWindowExW(WS_EX_TOPMOST | WS_EX_TOOLWINDOW, L"STATIC", L"",
                                        WS_POPUP | WS_BORDER | SS_LEFT,
                                        0, 0, 0, 0, m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_hoverLabelPopup, WM_SETFONT,
                 reinterpret_cast<WPARAM>(GetStockObject(DEFAULT_GUI_FONT)), TRUE);
    ShowWindow(m_hoverLabelPopup, SW_HIDE);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_WIN95_CLASSES };
    InitCommonControlsEx(&icc);
    m_tooltip = CreateWindowExW(WS_EX_TOPMOST, TOOLTIPS_CLASSW, nullptr,
                                WS_POPUP | TTS_ALWAYSTIP | TTS_NOPREFIX,
                                CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT,
                                m_hwnd, nullptr, m_hInstance, nullptr);
    SendMessageW(m_tooltip, TTM_SETMAXTIPWIDTH, 0, 640);
    SendMessageW(m_tooltip, TTM_SETDELAYTIME, TTDT_INITIAL, 0);
    SetWindowPos(m_tooltip, HWND_TOPMOST, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

    TOOLINFOW ti{};
    ti.cbSize = sizeof(ti);
    ti.uFlags = TTF_TRACK;
    ti.hwnd = m_hwnd;
    ti.uId = TOOLTIP_BASE_ID;
    ti.lpszText = const_cast<wchar_t*>(L"");
    SendMessageW(m_tooltip, TTM_ADDTOOLW, 0, reinterpret_cast<LPARAM>(&ti));

}

void PaddleConfigWindow::RefreshFromModel() {
    m_mappings = m_backend.GetActiveMappings();
    m_actions = m_backend.GetActiveActions();
    m_editProfileId = m_backend.GetActiveProfileId();
    m_gameSourceSpecs = m_backend.GetGameSourceSpecs();
    m_autoSwitchProfiles = m_loadAutoSwitch ? m_loadAutoSwitch() : false;
    RefreshGameSourcesUi();
    RefreshInstalledGames();
    const PaddleAction actionList[] = { m_actions.l4, m_actions.l5, m_actions.r4, m_actions.r5, m_actions.qam };
    for (int i = 0; i < kButtonCount; ++i) {
        switch (actionList[i].type) {
        case PaddleActionType::UseMenuMapping:
        case PaddleActionType::Gamepad:
            m_modeSelections[i] = 0;
            break;
        case PaddleActionType::KeyChord:
        case PaddleActionType::Macro:
            m_modeSelections[i] = 1;
            break;
        case PaddleActionType::None:
            m_modeSelections[i] = 2;
            break;
        }
    }
    RefreshProfileState();
    RefreshBindingSummary();
    Button_SetCheck(m_checkAutoSwitch, m_autoSwitchProfiles ? BST_CHECKED : BST_UNCHECKED);
    RefreshEditorForSelectedPaddle();
    InvalidateRect(m_hwnd, nullptr, FALSE);
}

void PaddleConfigWindow::RefreshProfileState() {
    SetWindowTextW(m_staticProfile, m_editProfileId == L"default" ? L"Default" : m_editProfileId.c_str());
}

void PaddleConfigWindow::RefreshBindingSummary() {
    if (!m_staticBindingSummary)
        return;
    SetWindowTextW(m_staticBindingSummary, PaddleLabelText(m_selectedPaddle).c_str());
}

void PaddleConfigWindow::RefreshGameSourcesUi() {
    SendMessageW(m_listGameSources, LB_RESETCONTENT, 0, 0);
    for (const std::wstring& spec : m_gameSourceSpecs) {
        const std::wstring display = GameSourceDisplayText(spec);
        SendMessageW(m_listGameSources, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(display.c_str()));
    }
    if (!m_gameSourceSpecs.empty())
        SendMessageW(m_listGameSources, LB_SETCURSEL, 0, 0);
    EnableWindow(m_buttonRemoveSource, !m_gameSourceSpecs.empty());
}

void PaddleConfigWindow::SetRefreshUiState(bool refreshing, const wchar_t* statusText) {
    SetWindowTextW(m_staticRefreshStatus, statusText ? statusText : L"");
    EnableWindow(m_buttonRefreshLibrary, !refreshing);
    EnableWindow(m_buttonAddFolder, !refreshing);
    EnableWindow(m_buttonAddExe, !refreshing);
    EnableWindow(m_buttonRemoveSource, !refreshing && !m_gameSourceSpecs.empty());
    EnableWindow(m_comboGameProfiles, !refreshing);
    EnableWindow(m_checkAutoSwitch, !refreshing);
    if (refreshing) {
        ShowWindow(m_progressRefresh, SW_SHOW);
        SendMessageW(m_progressRefresh, PBM_SETMARQUEE, TRUE, 0);
    } else {
        SendMessageW(m_progressRefresh, PBM_SETMARQUEE, FALSE, 0);
        ShowWindow(m_progressRefresh, SW_HIDE);
    }
    UpdateWindow(m_hwnd);
}

void PaddleConfigWindow::RefreshInstalledGames(bool forceRefresh) {
    if (forceRefresh) {
        SetRefreshUiState(true, L"Refreshing library...");
        MSG msg{};
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    m_installedGames = forceRefresh
        ? m_backend.RefreshInstalledGames()
        : m_backend.GetInstalledGames();
    if (m_editProfileId != L"default") {
        const auto it = std::find_if(m_installedGames.begin(), m_installedGames.end(), [&](const std::wstring& game) {
            return PaddleConfig::NormalizeProfileId(game) == m_editProfileId;
        });
        if (it == m_installedGames.end())
            m_installedGames.push_back(m_editProfileId);
    }

    std::sort(m_installedGames.begin(), m_installedGames.end());
    m_installedGames.erase(std::unique(m_installedGames.begin(), m_installedGames.end()), m_installedGames.end());
    m_installedGames.erase(std::remove_if(m_installedGames.begin(), m_installedGames.end(), [](const std::wstring& game) {
        return PaddleConfig::NormalizeProfileId(game) == L"default";
    }), m_installedGames.end());
    m_installedGames.insert(m_installedGames.begin(), L"default");

    SendMessageW(m_comboGameProfiles, CB_RESETCONTENT, 0, 0);
    int selectedIndex = -1;
    for (int i = 0; i < static_cast<int>(m_installedGames.size()); ++i) {
        const wchar_t* label = m_installedGames[i] == L"default" ? L"Default" : m_installedGames[i].c_str();
        SendMessageW(m_comboGameProfiles, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(label));
        if (PaddleConfig::NormalizeProfileId(m_installedGames[i]) == m_editProfileId)
            selectedIndex = i;
    }
    if (selectedIndex >= 0)
        SendMessageW(m_comboGameProfiles, CB_SETCURSEL, selectedIndex, 0);
    else if (!m_installedGames.empty())
        SendMessageW(m_comboGameProfiles, CB_SETCURSEL, 0, 0);

    if (forceRefresh) {
        const std::wstring status = std::to_wstring(m_installedGames.size()) + L" item(s) found";
        SetRefreshUiState(false, status.c_str());
    }
}

PaddleAction* PaddleConfigWindow::SelectedAction() {
    switch (m_selectedPaddle) {
    case 0: return &m_actions.l4;
    case 1: return &m_actions.l5;
    case 2: return &m_actions.r4;
    case 3: return &m_actions.r5;
    default: return &m_actions.qam;
    }
}

PaddleMapping* PaddleConfigWindow::SelectedMapping() {
    switch (m_selectedPaddle) {
    case 0: return &m_mappings.l4;
    case 1: return &m_mappings.l5;
    case 2: return &m_mappings.r4;
    case 3: return &m_mappings.r5;
    default: return &m_mappings.qam;
    }
}

void PaddleConfigWindow::RefreshEditorForSelectedPaddle() {
    if (!m_hwnd)
        return;

    m_updatingControls = true;
    SetWindowTextW(m_staticSelected, PaddleName(m_selectedPaddle));
    SendMessageW(m_comboPaddleSelect, CB_SETCURSEL, m_selectedPaddle, 0);
    PaddleAction* action = SelectedAction();
    PaddleMapping* mapping = SelectedMapping();

    int modeIndex = m_modeSelections[m_selectedPaddle];
    PaddleMapping gamepadTarget = *mapping;

    if (modeIndex == 0 && action->type == PaddleActionType::Gamepad) {
        gamepadTarget = action->gamepadMapping;
    }

    SendMessageW(m_comboMode, CB_SETCURSEL, modeIndex, 0);

    int gamepadIndex = 0;
    for (int i = 0; i < static_cast<int>(std::size(kGamepadOptions)); ++i) {
        if (kGamepadOptions[i] == gamepadTarget) {
            gamepadIndex = i;
            break;
        }
    }
    SendMessageW(m_comboGamepad, CB_SETCURSEL, gamepadIndex, 0);

    SetWindowTextW(m_editBinding, ActionTextForEditor(*action, *mapping).c_str());
    Button_SetCheck(m_checkRapid, action->rapidFire ? BST_CHECKED : BST_UNCHECKED);
    RefreshBindingSummary();
    UpdateControlState();
    m_updatingControls = false;
}

void PaddleConfigWindow::RefreshControllerFocus() {
    HWND target = ControllerFocusHwnd();
    if (target && IsWindowVisible(target) && IsWindowEnabled(target)) {
        InvalidateFocusOutline(GetFocus());
        SetFocus(target);
        InvalidateFocusOutline(target);
    }
}

void PaddleConfigWindow::InvalidateFocusOutline(HWND control) {
    if (!m_hwnd || !control)
        return;
    RECT rect{};
    if (!GetWindowRect(control, &rect))
        return;
    MapWindowPoints(HWND_DESKTOP, m_hwnd, reinterpret_cast<POINT*>(&rect), 2);
    InflateRect(&rect, 8, 8);
    InvalidateRect(m_hwnd, &rect, FALSE);
}

void PaddleConfigWindow::InvalidatePreviewArea() {
    if (!m_hwnd)
        return;

    RECT previewRect{ 20, 32, 580, 411 };
    InvalidateRect(m_hwnd, &previewRect, FALSE);

    if (m_staticBindingSummary) {
        RECT summaryRect{};
        if (GetWindowRect(m_staticBindingSummary, &summaryRect)) {
            MapWindowPoints(HWND_DESKTOP, m_hwnd, reinterpret_cast<POINT*>(&summaryRect), 2);
            InvalidateRect(m_hwnd, &summaryRect, FALSE);
        }
    }
}

void PaddleConfigWindow::UpdateControlState() {
    const int modeIndex = CurrentModeSelection();
    const bool gamepadMode = (modeIndex == 0);
    const bool textMode = (modeIndex == 1);
    const bool noneMode = (modeIndex == 2);
    EnableWindow(m_comboGamepad, gamepadMode);
    EnableWindow(m_editBinding, textMode);
    SetWindowTextW(m_staticBinding, textMode ? L"Shortcut or macro:" : L"Binding:");
    ShowWindow(m_staticBinding, textMode ? SW_SHOW : SW_HIDE);
    ShowWindow(m_editBinding, textMode ? SW_SHOW : SW_HIDE);
    SetWindowTextW(m_staticBindingHelp,
                   textMode
                       ? L"Controller: use Capture Input...\r\nShortcut: CTRL+SHIFT+M\r\nMacro: WIN+TAB, ALT+ENTER"
                       : L"");
    ShowWindow(m_staticBindingHelp, textMode ? SW_SHOW : SW_HIDE);
    EnableWindow(m_checkRapid, !noneMode);
    EnableWindow(m_buttonRecord, textMode);
    ShowWindow(m_buttonRecord, textMode ? SW_SHOW : SW_HIDE);
    RefreshProfileState();
    RefreshControllerFocus();
}

int PaddleConfigWindow::CurrentModeSelection() const {
    return m_modeSelections[m_selectedPaddle];
}

void PaddleConfigWindow::SetModeSelectionForCurrent(int modeIndex) {
    if (modeIndex < 0 || modeIndex > 2)
        modeIndex = 0;
    m_modeSelections[m_selectedPaddle] = modeIndex;
}

HWND PaddleConfigWindow::ControllerFocusHwnd() const {
    const HWND order[] = {
        m_comboGameProfiles,
        m_buttonRefreshLibrary,
        m_listGameSources,
        m_buttonAddFolder,
        m_buttonAddExe,
        m_buttonRemoveSource,
        m_checkAutoSwitch,
        m_comboPaddleSelect,
        m_comboMode,
        m_editBinding,
        m_comboGamepad,
        m_checkRapid,
        m_buttonRecord,
        GetDlgItem(m_hwnd, IDC_CLOSE),
    };
    constexpr int closeIndex = static_cast<int>(std::size(order)) - 1;
    int index = m_controllerFocusIndex;
    if (index < 0)
        index = 0;
    if (index > closeIndex)
        index = closeIndex;

    HWND target = order[index];
    if (!target)
        return m_comboGameProfiles;
    if (target == m_buttonRecord && !IsWindowVisible(m_buttonRecord))
        return GetDlgItem(m_hwnd, IDC_CLOSE);
    return target;
}

HWND PaddleConfigWindow::FocusedComboForController() const {
    HWND target = ControllerFocusHwnd();
    if (target == m_comboGameProfiles || target == m_comboPaddleSelect ||
        target == m_comboMode || target == m_comboGamepad) {
        return target;
    }
    return nullptr;
}

bool PaddleConfigWindow::IsControllerComboDropped(HWND combo) const {
    return combo && SendMessageW(combo, CB_GETDROPPEDSTATE, 0, 0) != 0;
}

void PaddleConfigWindow::ToggleControllerComboDropdown(HWND combo) {
    if (!combo)
        return;
    const BOOL show = IsControllerComboDropped(combo) ? FALSE : TRUE;
    SendMessageW(combo, CB_SHOWDROPDOWN, show, 0);
}

void PaddleConfigWindow::StepControllerComboSelection(HWND combo, int delta) {
    if (!combo)
        return;
    const int count = static_cast<int>(SendMessageW(combo, CB_GETCOUNT, 0, 0));
    if (count <= 0)
        return;
    int selectedIndex = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
    if (selectedIndex < 0)
        selectedIndex = 0;
    const int oldIndex = selectedIndex;
    selectedIndex += delta;
    if (selectedIndex < 0)
        selectedIndex = count - 1;
    if (selectedIndex >= count)
        selectedIndex = 0;
    m_updatingControls = true;
    SendMessageW(combo, CB_SETCURSEL, selectedIndex, 0);
    m_updatingControls = false;
    logging::Logf("[ControllerNav] Step combo id=%d old=%d new=%d dropped=%d",
                  GetDlgCtrlID(combo), oldIndex, selectedIndex,
                  IsControllerComboDropped(combo) ? 1 : 0);
}

void PaddleConfigWindow::CommitControllerComboSelection(HWND combo) {
    if (!combo)
        return;
    logging::Logf("[ControllerNav] Commit combo id=%d selection=%d",
                  GetDlgCtrlID(combo),
                  static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0)));
    if (combo == m_comboGameProfiles) {
        UseSelectedGameProfile();
    } else if (combo == m_comboPaddleSelect) {
        const int selectedIndex = static_cast<int>(SendMessageW(m_comboPaddleSelect, CB_GETCURSEL, 0, 0));
        if (selectedIndex >= 0 && selectedIndex < kButtonCount) {
            m_selectedPaddle = selectedIndex;
            logging::Logf("[ControllerNav] Selected paddle now=%d", m_selectedPaddle);
            RefreshEditorForSelectedPaddle();
            InvalidatePreviewArea();
        }
    } else if (combo == m_comboMode) {
        SetModeSelectionForCurrent(static_cast<int>(SendMessageW(m_comboMode, CB_GETCURSEL, 0, 0)));
        UpdateControlState();
        if (CurrentModeSelection() != 1)
            ApplySelection();
    } else if (combo == m_comboGamepad) {
        ApplySelection();
    }
}

void ReselectComboAfterControllerCommit(HWND combo, int selection) {
    if (!combo || selection < 0)
        return;
    SendMessageW(combo, CB_SETCURSEL, selection, 0);
}

void PaddleConfigWindow::MoveControllerFocus(int delta) {
    HWND oldFocus = GetFocus();
    if (!oldFocus || !IsChild(m_hwnd, oldFocus))
        oldFocus = ControllerFocusHwnd();
    const HWND order[] = {
        m_comboGameProfiles,
        m_buttonRefreshLibrary,
        m_listGameSources,
        m_buttonAddFolder,
        m_buttonAddExe,
        m_buttonRemoveSource,
        m_checkAutoSwitch,
        m_comboPaddleSelect,
        m_comboMode,
        m_editBinding,
        m_comboGamepad,
        m_checkRapid,
        m_buttonRecord,
        GetDlgItem(m_hwnd, IDC_CLOSE),
    };
    for (int i = 0; i < static_cast<int>(std::size(order)); ++i) {
        if (order[i] == oldFocus) {
            m_controllerFocusIndex = i;
            break;
        }
    }
    const int modeIndex = CurrentModeSelection();
    const bool gamepadMode = modeIndex == 0;
    const bool textMode = modeIndex == 1;
    const bool noneMode = modeIndex == 2;
    const HWND current = oldFocus;
    const HWND closeButton = GetDlgItem(m_hwnd, IDC_CLOSE);
    if (delta > 0) {
        if (textMode && current == m_comboMode) {
            FocusControllerControl(m_editBinding);
            return;
        }
        if (textMode && current == m_editBinding) {
            FocusControllerControl(m_checkRapid);
            return;
        }
        if (textMode && current == m_checkRapid && IsWindowVisible(m_buttonRecord)) {
            FocusControllerControl(m_buttonRecord);
            return;
        }
        if (textMode && current == m_buttonRecord) {
            FocusControllerControl(closeButton);
            return;
        }
        if (gamepadMode && current == m_comboMode) {
            FocusControllerControl(m_comboGamepad);
            return;
        }
        if (gamepadMode && current == m_comboGamepad) {
            FocusControllerControl(m_checkRapid);
            return;
        }
        if (gamepadMode && current == m_checkRapid) {
            FocusControllerControl(closeButton);
            return;
        }
        if (noneMode && current == m_comboMode) {
            FocusControllerControl(closeButton);
            return;
        }
        if (current == closeButton)
            return;
    } else if (delta < 0) {
        if (textMode && current == closeButton && IsWindowVisible(m_buttonRecord)) {
            FocusControllerControl(m_buttonRecord);
            return;
        }
        if (textMode && current == m_buttonRecord) {
            FocusControllerControl(m_checkRapid);
            return;
        }
        if (textMode && current == m_checkRapid) {
            FocusControllerControl(m_editBinding);
            return;
        }
        if (textMode && current == m_editBinding) {
            FocusControllerControl(m_comboMode);
            return;
        }
        if (textMode && current == m_comboMode) {
            FocusControllerControl(m_comboPaddleSelect);
            return;
        }
        if (gamepadMode && current == closeButton) {
            FocusControllerControl(m_checkRapid);
            return;
        }
        if (gamepadMode && current == m_checkRapid) {
            FocusControllerControl(m_comboGamepad);
            return;
        }
        if (gamepadMode && current == m_comboGamepad) {
            FocusControllerControl(m_comboMode);
            return;
        }
        if (gamepadMode && current == m_comboMode) {
            FocusControllerControl(m_comboPaddleSelect);
            return;
        }
        if (noneMode && current == closeButton) {
            FocusControllerControl(m_comboMode);
            return;
        }
        if (noneMode && current == m_comboMode) {
            FocusControllerControl(m_comboPaddleSelect);
            return;
        }
    }

    constexpr int focusCount = static_cast<int>(std::size(order));
    for (int i = 0; i < focusCount; ++i) {
        m_controllerFocusIndex += delta;
        if (m_controllerFocusIndex < 0)
            m_controllerFocusIndex = focusCount - 1;
        if (m_controllerFocusIndex >= focusCount)
            m_controllerFocusIndex = 0;

        HWND target = ControllerFocusHwnd();
        if (target && IsWindowVisible(target) && IsWindowEnabled(target)) {
            InvalidateFocusOutline(oldFocus);
            SetFocus(target);
            InvalidateFocusOutline(target);
            return;
        }
    }
}

bool PaddleConfigWindow::FocusControllerControl(HWND target) {
    if (!target)
        return false;

    const HWND previous = GetFocus();

    const HWND order[] = {
        m_comboGameProfiles,
        m_buttonRefreshLibrary,
        m_listGameSources,
        m_buttonAddFolder,
        m_buttonAddExe,
        m_buttonRemoveSource,
        m_checkAutoSwitch,
        m_comboPaddleSelect,
        m_comboMode,
        m_editBinding,
        m_comboGamepad,
        m_checkRapid,
        m_buttonRecord,
        GetDlgItem(m_hwnd, IDC_CLOSE),
    };

    for (int i = 0; i < static_cast<int>(std::size(order)); ++i) {
        if (order[i] != target)
            continue;
        m_controllerFocusIndex = i;
        logging::Logf("[ControllerNav] Focus direct from=%d to=%d",
                      previous ? GetDlgCtrlID(previous) : 0,
                      GetDlgCtrlID(target));
        RefreshControllerFocus();
        return true;
    }
    return false;
}

void PaddleConfigWindow::CycleCurrentProfile(int delta) {
    if (m_installedGames.empty())
        return;
    int selectedIndex = static_cast<int>(SendMessageW(m_comboGameProfiles, CB_GETCURSEL, 0, 0));
    if (selectedIndex < 0)
        selectedIndex = 0;
    selectedIndex += delta;
    if (selectedIndex < 0)
        selectedIndex = static_cast<int>(m_installedGames.size()) - 1;
    if (selectedIndex >= static_cast<int>(m_installedGames.size()))
        selectedIndex = 0;
    SendMessageW(m_comboGameProfiles, CB_SETCURSEL, selectedIndex, 0);
    UseSelectedGameProfile();
}

void PaddleConfigWindow::CycleCurrentPaddle(int delta) {
    m_selectedPaddle += delta;
    if (m_selectedPaddle < 0)
        m_selectedPaddle = kButtonCount - 1;
    if (m_selectedPaddle >= kButtonCount)
        m_selectedPaddle = 0;
    RefreshEditorForSelectedPaddle();
    InvalidatePreviewArea();
}

void PaddleConfigWindow::CycleCurrentMode(int delta) {
    int modeIndex = CurrentModeSelection() + delta;
    if (modeIndex < 0)
        modeIndex = 2;
    if (modeIndex > 2)
        modeIndex = 0;
    SetModeSelectionForCurrent(modeIndex);
    SendMessageW(m_comboMode, CB_SETCURSEL, modeIndex, 0);
    UpdateControlState();
    if (modeIndex != 1)
        ApplySelection();
}

void PaddleConfigWindow::CycleCurrentGamepad(int delta) {
    int selectedIndex = static_cast<int>(SendMessageW(m_comboGamepad, CB_GETCURSEL, 0, 0));
    if (selectedIndex < 0)
        selectedIndex = 0;
    const int optionCount = static_cast<int>(std::size(kGamepadOptions));
    selectedIndex += delta;
    if (selectedIndex < 0)
        selectedIndex = optionCount - 1;
    if (selectedIndex >= optionCount)
        selectedIndex = 0;
    SendMessageW(m_comboGamepad, CB_SETCURSEL, selectedIndex, 0);
    ApplySelection();
}

void PaddleConfigWindow::CycleGameSourceSelection(int delta) {
    if (m_gameSourceSpecs.empty())
        return;
    int selected = static_cast<int>(SendMessageW(m_listGameSources, LB_GETCURSEL, 0, 0));
    if (selected < 0)
        selected = 0;
    selected += delta;
    if (selected < 0)
        selected = static_cast<int>(m_gameSourceSpecs.size()) - 1;
    if (selected >= static_cast<int>(m_gameSourceSpecs.size()))
        selected = 0;
    SendMessageW(m_listGameSources, LB_SETCURSEL, selected, 0);
}

void PaddleConfigWindow::ToggleAutoSwitch() {
    const bool enabled = Button_GetCheck(m_checkAutoSwitch) != BST_CHECKED;
    Button_SetCheck(m_checkAutoSwitch, enabled ? BST_CHECKED : BST_UNCHECKED);
    m_autoSwitchProfiles = enabled;
    if (m_saveAutoSwitch)
        m_saveAutoSwitch(enabled);
}

void PaddleConfigWindow::ActivateFocusedControl() {
    HWND target = ControllerFocusHwnd();
    if (!target)
        return;

    if (target == m_comboGameProfiles) {
        ToggleControllerComboDropdown(target);
        return;
    }
    if (target == m_listGameSources) {
        return;
    }
    if (target == m_checkAutoSwitch) {
        ToggleAutoSwitch();
        return;
    }
    if (target == m_comboPaddleSelect) {
        ToggleControllerComboDropdown(target);
        return;
    }
    if (target == m_comboMode) {
        ToggleControllerComboDropdown(target);
        return;
    }
    if (target == m_editBinding) {
        if (CurrentModeSelection() == 1) {
            RecordMacro();
            return;
        }
        SetFocus(target);
        SendMessageW(target, EM_SETSEL, 0, -1);
        return;
    }
    if (target == m_comboGamepad) {
        ToggleControllerComboDropdown(target);
        return;
    }
    if (target == m_checkRapid) {
        Button_SetCheck(m_checkRapid,
                        Button_GetCheck(m_checkRapid) == BST_CHECKED ? BST_UNCHECKED : BST_CHECKED);
        ApplySelection();
        return;
    }

    SendMessageW(target, BM_CLICK, 0, 0);
}

void PaddleConfigWindow::HandleControllerTimer() {
    if (!m_controllerUiStateFn || !m_hwnd || !IsWindowVisible(m_hwnd) || IsIconic(m_hwnd) || !IsWindowEnabled(m_hwnd))
        return;

    const ControllerUiState current = m_controllerUiStateFn();
    auto pressed = [&](bool ControllerUiState::* member) {
        return (current.*member) && !(m_lastControllerUiState.*member);
    };
    auto focusedTarget = [&]() -> HWND {
        HWND focus = GetFocus();
        if (focus && IsChild(m_hwnd, focus))
            return focus;
        return ControllerFocusHwnd();
    };
    const HWND focused = focusedTarget();
    HWND combo = nullptr;
    if (focused == m_comboGameProfiles || focused == m_comboPaddleSelect ||
        focused == m_comboMode || focused == m_comboGamepad) {
        combo = focused;
    }
    const bool comboDropped = IsControllerComboDropped(combo);

    if (pressed(&ControllerUiState::back)) {
        logging::Logf("[ControllerNav] Back pressed comboDropped=%d focusId=%d",
                      comboDropped ? 1 : 0,
                      focused ? GetDlgCtrlID(focused) : 0);
        if (comboDropped) {
            SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
            m_lastControllerUiState = current;
            return;
        }
        CommitPendingChanges();
        DestroyWindow(m_hwnd);
        m_lastControllerUiState = current;
        return;
    }
    if (pressed(&ControllerUiState::previous))
        CycleCurrentPaddle(-1);
    if (pressed(&ControllerUiState::next))
        CycleCurrentPaddle(1);
    if (pressed(&ControllerUiState::up)) {
        if (comboDropped)
            StepControllerComboSelection(combo, -1);
        else {
            const HWND target = focusedTarget();
            const int modeIndex = CurrentModeSelection();
            if (modeIndex == 1 && target == GetDlgItem(m_hwnd, IDC_CLOSE) && IsWindowVisible(m_buttonRecord))
                FocusControllerControl(m_buttonRecord);
            else if (modeIndex == 0 && target == GetDlgItem(m_hwnd, IDC_CLOSE))
                FocusControllerControl(m_checkRapid);
            else if (modeIndex == 2 && target == GetDlgItem(m_hwnd, IDC_CLOSE))
                FocusControllerControl(m_comboMode);
            else
                MoveControllerFocus(-1);
        }
    }
    if (pressed(&ControllerUiState::down)) {
        if (comboDropped)
            StepControllerComboSelection(combo, 1);
        else {
            const HWND target = focusedTarget();
            const int modeIndex = CurrentModeSelection();
            if (modeIndex == 1 && target == m_buttonRecord)
                FocusControllerControl(GetDlgItem(m_hwnd, IDC_CLOSE));
            else if ((modeIndex == 1 || modeIndex == 0 || modeIndex == 2) && target == GetDlgItem(m_hwnd, IDC_CLOSE))
                return;
            else
                MoveControllerFocus(1);
        }
    }
    if (pressed(&ControllerUiState::left)) {
        HWND target = focusedTarget();
        if (comboDropped) {
            StepControllerComboSelection(combo, -1);
            CommitControllerComboSelection(combo);
        } else if (target == m_comboGameProfiles)
            CycleCurrentProfile(-1);
        else if (target == m_listGameSources)
            CycleGameSourceSelection(-1);
        else if (target == m_checkAutoSwitch)
            ToggleAutoSwitch();
        else if (target == m_comboPaddleSelect)
            CycleCurrentPaddle(-1);
        else if (target == m_comboMode)
            CycleCurrentMode(-1);
        else if (target == m_comboGamepad)
            CycleCurrentGamepad(-1);
        else if (target == m_checkRapid) {
            Button_SetCheck(m_checkRapid, BST_UNCHECKED);
            ApplySelection();
        }
    }
    if (pressed(&ControllerUiState::right)) {
        HWND target = focusedTarget();
        if (comboDropped) {
            StepControllerComboSelection(combo, 1);
            CommitControllerComboSelection(combo);
        } else if (target == m_comboGameProfiles)
            CycleCurrentProfile(1);
        else if (target == m_listGameSources)
            CycleGameSourceSelection(1);
        else if (target == m_checkAutoSwitch)
            ToggleAutoSwitch();
        else if (target == m_comboPaddleSelect)
            CycleCurrentPaddle(1);
        else if (target == m_comboMode)
            CycleCurrentMode(1);
        else if (target == m_comboGamepad)
            CycleCurrentGamepad(1);
        else if (target == m_checkRapid) {
            Button_SetCheck(m_checkRapid, BST_CHECKED);
            ApplySelection();
        }
    }
    if (pressed(&ControllerUiState::confirm)) {
        logging::Logf("[ControllerNav] Confirm pressed comboDropped=%d focusId=%d",
                      comboDropped ? 1 : 0,
                      focusedTarget() ? GetDlgCtrlID(focusedTarget()) : 0);
        if (comboDropped) {
            const int committedSelection = static_cast<int>(SendMessageW(combo, CB_GETCURSEL, 0, 0));
            CommitControllerComboSelection(combo);
            SendMessageW(combo, CB_SHOWDROPDOWN, FALSE, 0);
            m_updatingControls = true;
            ReselectComboAfterControllerCommit(combo, committedSelection);
            m_updatingControls = false;
        } else {
            ActivateFocusedControl();
        }
    }
    if (pressed(&ControllerUiState::record) && IsWindowVisible(m_buttonRecord))
        RecordMacro();

    m_lastControllerUiState = current;
}

void PaddleConfigWindow::RecordMacro() {
    wchar_t buffer[512] = {};
    GetWindowTextW(m_editBinding, buffer, static_cast<int>(std::size(buffer)));
    std::wstring macroText;
    if (!MacroRecorder::Record(m_hwnd, macroText, buffer, m_controllerChordFn, m_controllerUiStateFn))
        return;

    SetWindowTextW(m_editBinding, macroText.c_str());
    SetModeSelectionForCurrent(1);
    ApplySelection();
}

void PaddleConfigWindow::CommitPendingChanges() {
    if (m_updatingControls)
        return;
    ApplySelection();
}

void PaddleConfigWindow::PersistCurrentState() {
    m_backend.SaveActiveProfile(m_mappings, m_actions);
    if (m_onApplyProfile)
        m_onApplyProfile(m_backend.GetActiveProfileId(), true);
}

void PaddleConfigWindow::UseSelectedGameProfile() {
    const int selectedIndex = static_cast<int>(SendMessageW(m_comboGameProfiles, CB_GETCURSEL, 0, 0));
    if (selectedIndex < 0 || selectedIndex >= static_cast<int>(m_installedGames.size()))
        return;
    m_backend.SetActiveProfileId(m_installedGames[selectedIndex], true);
    if (m_onApplyProfile)
        m_onApplyProfile(m_backend.GetActiveProfileId(), true);
    RefreshFromModel();
}

void PaddleConfigWindow::AddGameFolderSource() {
    BROWSEINFOW bi{};
    bi.hwndOwner = m_hwnd;
    bi.lpszTitle = L"Choose your Steam library or games folder";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;

    PIDLIST_ABSOLUTE pidl = SHBrowseForFolderW(&bi);
    if (!pidl)
        return;

    wchar_t path[MAX_PATH] = {};
    if (SHGetPathFromIDListW(pidl, path)) {
        m_gameSourceSpecs.push_back(L"DIR|" + std::wstring(path));
        m_backend.SetGameSourceSpecs(m_gameSourceSpecs);
        RefreshGameSourcesUi();
        RefreshInstalledGames(true);
        RefreshProfileState();
    }
    CoTaskMemFree(pidl);
}

void PaddleConfigWindow::AddGameExeSource() {
    OPENFILENAMEW ofn{};
    wchar_t path[MAX_PATH] = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = m_hwnd;
    ofn.lpstrFilter = L"Executable Files\0*.exe\0All Files\0*.*\0";
    ofn.lpstrFile = path;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    if (GetOpenFileNameW(&ofn)) {
        m_gameSourceSpecs.push_back(L"EXE|" + std::wstring(path));
        m_backend.SetGameSourceSpecs(m_gameSourceSpecs);
        RefreshGameSourcesUi();
        RefreshInstalledGames(true);
        RefreshProfileState();
    }
}

void PaddleConfigWindow::RemoveSelectedGameSource() {
    const int selected = static_cast<int>(SendMessageW(m_listGameSources, LB_GETCURSEL, 0, 0));
    if (selected < 0 || selected >= static_cast<int>(m_gameSourceSpecs.size()))
        return;
    m_gameSourceSpecs.erase(m_gameSourceSpecs.begin() + selected);
    m_backend.SetGameSourceSpecs(m_gameSourceSpecs);
    RefreshGameSourcesUi();
    RefreshInstalledGames(true);
    RefreshProfileState();
}

void PaddleConfigWindow::ApplySelection() {
    PaddleAction* action = SelectedAction();
    PaddleMapping* mapping = SelectedMapping();
    const int modeIndex = CurrentModeSelection();

    if (modeIndex == 0) {
        const int gamepadIndex = static_cast<int>(SendMessageW(m_comboGamepad, CB_GETCURSEL, 0, 0));
        const PaddleMapping selected = (gamepadIndex >= 0 && gamepadIndex < static_cast<int>(std::size(kGamepadOptions)))
            ? kGamepadOptions[gamepadIndex]
            : PaddleMapping::None;
        *mapping = selected;
        action->type = PaddleActionType::Gamepad;
        action->gamepadMapping = selected;
        action->chord.clear();
        action->macroSteps.clear();
    } else if (modeIndex == 1) {
        wchar_t buffer[512] = {};
        GetWindowTextW(m_editBinding, buffer, static_cast<int>(std::size(buffer)));
        std::wstring raw = buffer;
        if (raw.empty()) {
            logging::Logf("[ControllerNav] Deferred mode save id=%d mode=%d emptyBinding=1",
                          m_selectedPaddle, modeIndex);
            RefreshBindingSummary();
            InvalidatePreviewArea();
            return;
        }
        const bool isMacro = raw.find(L',') != std::wstring::npos;
        std::wstring prefixed = (isMacro ? L"macro:" : L"key:") + raw;
        PaddleAction parsed{};
        if (PaddleConfig::ParseActionString(prefixed, parsed)) {
            *action = std::move(parsed);
        } else {
            logging::Logf("[ControllerNav] Deferred mode save id=%d mode=%d invalidBinding=1 raw=%s",
                          m_selectedPaddle, modeIndex, logging::Narrow(raw).c_str());
            RefreshBindingSummary();
            InvalidatePreviewArea();
            return;
        }
    } else {
        *action = PaddleAction{PaddleActionType::None};
    }

    action->rapidFire = Button_GetCheck(m_checkRapid) == BST_CHECKED;
    if (action->type == PaddleActionType::None || action->type == PaddleActionType::UseMenuMapping) {
        action->rapidFire = false;
    }

    PersistCurrentState();
    RefreshBindingSummary();
    InvalidatePreviewArea();
}

RECT PaddleConfigWindow::PaddleRect(int paddleIndex) const {
    switch (paddleIndex) {
    case 0: return RECT{28, 224, 152, 260};
    case 1: return RECT{28, 278, 152, 314};
    case 2: return RECT{438, 224, 562, 260};
    case 3: return RECT{438, 278, 562, 314};
    default: return RECT{234, 328, 358, 364};
    }
}

POINT PaddleConfigWindow::PaddleAnchor(int paddleIndex) const {
    switch (paddleIndex) {
    case 0: return POINT{192, 251};
    case 1: return POINT{180, 306};
    case 2: return POINT{410, 251};
    case 3: return POINT{421, 306};
    default: return POINT{295, 300};
    }
}

std::wstring PaddleConfigWindow::PaddleLabelText(int paddleIndex) const {
    const PaddleAction paddleActions[] = { m_actions.l4, m_actions.l5, m_actions.r4, m_actions.r5, m_actions.qam };
    const PaddleMapping paddleMappings[] = { m_mappings.l4, m_mappings.l5, m_mappings.r4, m_mappings.r5, m_mappings.qam };
    if (paddleIndex < 0 || paddleIndex >= kButtonCount)
        return {};
    return std::wstring(PaddleName(paddleIndex)) + L": " +
        PaddleConfig::Describe(paddleActions[paddleIndex], paddleMappings[paddleIndex]);
}

int PaddleConfigWindow::HitTestPaddleLabel(POINT pt) const {
    for (int i = 0; i < kButtonCount; ++i) {
        RECT rect = PaddleRect(i);
        if (PtInRect(&rect, pt))
            return i;
    }
    return -1;
}

void PaddleConfigWindow::Paint(HDC hdc) {
    static std::unique_ptr<Gdiplus::Image> controllerImage(LoadControllerImage());
    RECT client{};
    GetClientRect(m_hwnd, &client);
    HBRUSH bg = CreateSolidBrush(RGB(245, 246, 248));
    FillRect(hdc, &client, bg);
    DeleteObject(bg);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, RGB(30, 33, 38));

    if (controllerImage && controllerImage->GetLastStatus() == Gdiplus::Ok) {
        Gdiplus::Graphics graphics(hdc);
        graphics.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
        graphics.DrawImage(controllerImage.get(), Gdiplus::Rect(20, 32, 560, 379));
    } else {
        HPEN pen = CreatePen(PS_SOLID, 2, RGB(60, 68, 80));
        HGDIOBJ oldPen = SelectObject(hdc, pen);
        HBRUSH body = CreateSolidBrush(RGB(205, 211, 220));
        HGDIOBJ oldBrush = SelectObject(hdc, body);
        RoundRect(hdc, 60, 80, 540, 410, 120, 120);
        SelectObject(hdc, oldBrush);
        DeleteObject(body);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }

    for (int i = 0; i < kButtonCount; ++i) {
        RECT rect = PaddleRect(i);
        COLORREF fill = (i == m_selectedPaddle) ? RGB(30, 120, 200) : RGB(255, 255, 255);
        COLORREF text = (i == m_selectedPaddle) ? RGB(255, 255, 255) : RGB(20, 24, 28);
        HBRUSH pill = CreateSolidBrush(fill);
        FillRect(hdc, &rect, pill);
        DeleteObject(pill);
        FrameRect(hdc, &rect, reinterpret_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));

        POINT anchor = PaddleAnchor(i);
        HPEN arrowPen = CreatePen(PS_SOLID, 2, fill);
        HGDIOBJ oldArrowPen = SelectObject(hdc, arrowPen);
        const int midY = (rect.top + rect.bottom) / 2;
        const int targetX = (i < 2) ? rect.right : ((i < 4) ? rect.left : (rect.left + rect.right) / 2);
        MoveToEx(hdc, anchor.x, anchor.y, nullptr);
        LineTo(hdc, targetX, midY);
        if (i < 2) {
            LineTo(hdc, targetX - 10, midY - 6);
            MoveToEx(hdc, targetX, midY, nullptr);
            LineTo(hdc, targetX - 10, midY + 6);
        } else if (i < 4) {
            LineTo(hdc, targetX + 10, midY - 6);
            MoveToEx(hdc, targetX, midY, nullptr);
            LineTo(hdc, targetX + 10, midY + 6);
        } else {
            LineTo(hdc, targetX - 6, midY + 10);
            MoveToEx(hdc, targetX, midY, nullptr);
            LineTo(hdc, targetX + 6, midY + 10);
        }
        SelectObject(hdc, oldArrowPen);
        DeleteObject(arrowPen);

        std::wstring label = PaddleLabelText(i);
        SetTextColor(hdc, text);
        RECT textRect = rect;
        textRect.left += 6;
        DrawTextW(hdc, label.c_str(), -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
    }

    if (HWND focused = ControllerFocusHwnd()) {
        RECT focusRect{};
        if (GetWindowRect(focused, &focusRect)) {
            MapWindowPoints(HWND_DESKTOP, m_hwnd, reinterpret_cast<POINT*>(&focusRect), 2);
            InflateRect(&focusRect, 4, 4);
            HPEN focusPen = CreatePen(PS_SOLID, 3, RGB(0, 120, 212));
            HGDIOBJ oldPen = SelectObject(hdc, focusPen);
            HGDIOBJ oldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
            RoundRect(hdc, focusRect.left, focusRect.top, focusRect.right, focusRect.bottom, 12, 12);
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(focusPen);
        }
    }
}
