#include "TrayApp.h"
#include "ControllerManager.h"
#include "PaddleConfig.h"
#include "PaddleConfigWindow.h"
#include "SteamLibrary.h"
#include "logging/Log.h"
#include "resource.h"
#include <algorithm>
#include <chrono>
#include <cctype>
#include <dbt.h>
#include <shellapi.h>
#include <sstream>
#include <string>
#include <thread>
#include <tlhelp32.h>
#include <winreg.h>

static TrayApp* g_app = nullptr;

static constexpr wchar_t WNDCLASS_NAME[] = L"SteamControllerRemapperTray";
static constexpr wchar_t REG_KEY[]       = L"Software\\SteamControllerRemapper";
static constexpr wchar_t LEGACY_REG_KEY[] = L"Software\\XboxModeSteamlessController";
static constexpr wchar_t REG_RUN_KEY[]   = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
static constexpr wchar_t APP_NAME[]      = L"Steam Controller Remapper";
static constexpr wchar_t LEGACY_APP_NAME[] = L"Xbox Mode Steamless Controller";
static constexpr wchar_t OLD_APP_NAME[]  = L"SteamlessController";
static constexpr wchar_t REG_LAST_PROFILE[] = L"LastActiveProfileId";
static constexpr wchar_t REG_MANUAL_OVERRIDE[] = L"ManualProfileOverride";

static bool HasRunEntry(const wchar_t* name) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_READ, &key) != ERROR_SUCCESS)
        return false;

    bool exists = RegQueryValueExW(key, name, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS;
    RegCloseKey(key);
    return exists;
}

static bool HasAnyRunEntry() {
    return HasRunEntry(APP_NAME) || HasRunEntry(LEGACY_APP_NAME) || HasRunEntry(OLD_APP_NAME);
}

static bool OpenSettingsKeyForRead(HKEY& key) {
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS)
        return true;
    return RegOpenKeyExW(HKEY_CURRENT_USER, LEGACY_REG_KEY, 0, KEY_READ, &key) == ERROR_SUCCESS;
}

static std::wstring WidenUtf8(const std::string& value) {
    if (value.empty())
        return {};
    const int needed = MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), nullptr, 0);
    if (needed <= 0)
        return {};
    std::wstring out(static_cast<size_t>(needed), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, value.c_str(), static_cast<int>(value.size()), out.data(), needed);
    return out;
}

static std::string TrimAscii(std::string value) {
    auto isSpace = [](unsigned char ch) { return std::isspace(ch) != 0; };
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.front())))
        value.erase(value.begin());
    while (!value.empty() && isSpace(static_cast<unsigned char>(value.back())))
        value.pop_back();
    return value;
}

static std::string JsonEscape(const std::wstring& value) {
    std::ostringstream out;
    const std::string utf8 = logging::Narrow(value);
    for (unsigned char ch : utf8) {
        switch (ch) {
        case '\\': out << "\\\\"; break;
        case '"': out << "\\\""; break;
        case '\b': out << "\\b"; break;
        case '\f': out << "\\f"; break;
        case '\n': out << "\\n"; break;
        case '\r': out << "\\r"; break;
        case '\t': out << "\\t"; break;
        default:
            if (ch < 0x20) {
                char buffer[7];
                std::snprintf(buffer, sizeof(buffer), "\\u%04x", ch);
                out << buffer;
            } else {
                out << static_cast<char>(ch);
            }
        }
    }
    return out.str();
}

static void SendWinGToggle() {
    INPUT inputs[4]{};
    inputs[0].type = INPUT_KEYBOARD;
    inputs[0].ki.wVk = VK_LWIN;
    inputs[1].type = INPUT_KEYBOARD;
    inputs[1].ki.wVk = 'G';
    inputs[2].type = INPUT_KEYBOARD;
    inputs[2].ki.wVk = 'G';
    inputs[2].ki.dwFlags = KEYEVENTF_KEYUP;
    inputs[3].type = INPUT_KEYBOARD;
    inputs[3].ki.wVk = VK_LWIN;
    inputs[3].ki.dwFlags = KEYEVENTF_KEYUP;
    SendInput(static_cast<UINT>(std::size(inputs)), inputs, sizeof(INPUT));
}

static void DismissGameBarAndRefocus(HWND editor) {
    std::thread([editor]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(700));
        logging::Logf("[WidgetBridge] Sending Win+G toggle to dismiss overlay");
        SendWinGToggle();
        std::this_thread::sleep_for(std::chrono::milliseconds(220));
        if (editor && IsWindow(editor)) {
            ShowWindow(editor, SW_SHOWNORMAL);
            BringWindowToTop(editor);
            SetForegroundWindow(editor);
            SetFocus(editor);
            logging::Logf("[WidgetBridge] Refocused editor hwnd=%p after overlay dismiss", editor);
        }
    }).detach();
}

static std::string JsonBool(bool value) {
    return value ? "true" : "false";
}

static std::string MappingToken(PaddleMapping mapping) {
    switch (mapping) {
    case PaddleMapping::A: return "A";
    case PaddleMapping::B: return "B";
    case PaddleMapping::X: return "X";
    case PaddleMapping::Y: return "Y";
    case PaddleMapping::LeftShoulder: return "LeftShoulder";
    case PaddleMapping::RightShoulder: return "RightShoulder";
    case PaddleMapping::View: return "View";
    case PaddleMapping::Menu: return "Menu";
    case PaddleMapping::LeftThumb: return "LeftThumb";
    case PaddleMapping::RightThumb: return "RightThumb";
    case PaddleMapping::Guide: return "Guide";
    case PaddleMapping::DPadUp: return "DPadUp";
    case PaddleMapping::DPadRight: return "DPadRight";
    case PaddleMapping::DPadDown: return "DPadDown";
    case PaddleMapping::DPadLeft: return "DPadLeft";
    case PaddleMapping::None:
    default:
        return "None";
    }
}

static std::string ActionTypeToken(PaddleActionType type) {
    switch (type) {
    case PaddleActionType::UseMenuMapping: return "UseMenuMapping";
    case PaddleActionType::None: return "None";
    case PaddleActionType::Gamepad: return "Gamepad";
    case PaddleActionType::KeyChord: return "KeyChord";
    case PaddleActionType::Macro: return "Macro";
    default: return "Unknown";
    }
}

static bool TryParseMappingToken(const std::wstring& token, PaddleMapping& mapping) {
    PaddleAction parsed{};
    if (!PaddleConfig::ParseActionString(L"gamepad:" + token, parsed) ||
        parsed.type != PaddleActionType::Gamepad) {
        return false;
    }
    mapping = parsed.gamepadMapping;
    return true;
}

static PaddleAction* GetPaddleAction(PaddleActionBindings& bindings, const std::wstring& paddleId) {
    if (_wcsicmp(paddleId.c_str(), L"L4") == 0) return &bindings.l4;
    if (_wcsicmp(paddleId.c_str(), L"L5") == 0) return &bindings.l5;
    if (_wcsicmp(paddleId.c_str(), L"R4") == 0) return &bindings.r4;
    if (_wcsicmp(paddleId.c_str(), L"R5") == 0) return &bindings.r5;
    if (_wcsicmp(paddleId.c_str(), L"QAM") == 0) return &bindings.qam;
    return nullptr;
}

static PaddleMapping* GetPaddleMapping(PaddleMappings& mappings, const std::wstring& paddleId) {
    if (_wcsicmp(paddleId.c_str(), L"L4") == 0) return &mappings.l4;
    if (_wcsicmp(paddleId.c_str(), L"L5") == 0) return &mappings.l5;
    if (_wcsicmp(paddleId.c_str(), L"R4") == 0) return &mappings.r4;
    if (_wcsicmp(paddleId.c_str(), L"R5") == 0) return &mappings.r5;
    if (_wcsicmp(paddleId.c_str(), L"QAM") == 0) return &mappings.qam;
    return nullptr;
}

static void AppendBindingJson(std::ostringstream& json,
                              const char* name,
                              const PaddleAction& action,
                              PaddleMapping fallback) {
    const PaddleMapping effectiveMapping =
        action.type == PaddleActionType::Gamepad ? action.gamepadMapping : fallback;
    json << "\"" << name << "\":{"
         << "\"display\":\"" << JsonEscape(PaddleConfig::Describe(action, fallback)) << "\","
         << "\"actionType\":\"" << ActionTypeToken(action.type) << "\","
         << "\"mappingToken\":\"" << MappingToken(effectiveMapping) << "\","
         << "\"rapidFire\":" << JsonBool(action.rapidFire)
         << "}";
}

static std::wstring GetLocalAppDataPath() {
    wchar_t path[MAX_PATH] = {};
    const DWORD size = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (size == 0 || size >= MAX_PATH)
        return {};
    return path;
}

static std::wstring GetWidgetLocalStateDirectory() {
    const std::wstring localAppData = GetLocalAppDataPath();
    if (localAppData.empty())
        return {};

    const std::wstring packagesRoot = localAppData + L"\\Packages";
    WIN32_FIND_DATAW findData{};
    HANDLE find = FindFirstFileW((packagesRoot + L"\\SteamControllerRemapperWidget_*").c_str(), &findData);
    if (find == INVALID_HANDLE_VALUE)
        return {};

    std::wstring localStatePath;
    do {
        if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) == 0)
            continue;
        if (wcscmp(findData.cFileName, L".") == 0 || wcscmp(findData.cFileName, L"..") == 0)
            continue;
        localStatePath = packagesRoot + L"\\" + findData.cFileName + L"\\LocalState";
        break;
    } while (FindNextFileW(find, &findData));
    FindClose(find);
    return localStatePath;
}

static bool WriteUtf8File(const std::wstring& path, const std::string& content) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"wb") != 0 || !file)
        return false;
    const size_t written = std::fwrite(content.data(), 1, content.size(), file);
    std::fclose(file);
    return written == content.size();
}

static std::string ReadUtf8File(const std::wstring& path) {
    FILE* file = nullptr;
    if (_wfopen_s(&file, path.c_str(), L"rb") != 0 || !file)
        return {};
    std::string content;
    char buffer[1024];
    size_t read = 0;
    while ((read = std::fread(buffer, 1, sizeof(buffer), file)) > 0)
        content.append(buffer, buffer + read);
    std::fclose(file);
    return content;
}

static std::vector<std::string> SplitLines(const std::string& text) {
    std::vector<std::string> lines;
    std::istringstream stream(text);
    std::string line;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        lines.push_back(line);
    }
    return lines;
}

static bool IsProcessRunningByName(const wchar_t* processName) {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, processName) == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return running;
}

static std::wstring GetProcessPathById(DWORD pid) {
    HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, pid);
    if (!process)
        return {};

    wchar_t buffer[MAX_PATH] = {};
    DWORD size = static_cast<DWORD>(std::size(buffer));
    std::wstring path;
    if (QueryFullProcessImageNameW(process, 0, buffer, &size))
        path.assign(buffer, size);
    CloseHandle(process);
    return path;
}

static std::vector<std::wstring> GetRunningProcessPaths() {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return {};

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    std::vector<std::wstring> processPaths;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (entry.th32ProcessID == 0 || entry.th32ProcessID == GetCurrentProcessId())
                continue;
            std::wstring processPath = GetProcessPathById(entry.th32ProcessID);
            if (!processPath.empty())
                processPaths.push_back(std::move(processPath));
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return processPaths;
}

static void LogLaunchContext() {
    wchar_t exePath[MAX_PATH] = {};
    GetModuleFileNameW(nullptr, exePath, MAX_PATH);

    HWND shellWindow = GetShellWindow();
    DWORD shellPid = 0;
    wchar_t shellClass[128] = {};
    if (shellWindow) {
        GetWindowThreadProcessId(shellWindow, &shellPid);
        GetClassNameW(shellWindow, shellClass, static_cast<int>(std::size(shellClass)));
    }

    std::wstring shellProcess;
    if (shellPid != 0) {
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot != INVALID_HANDLE_VALUE) {
            PROCESSENTRY32W entry{};
            entry.dwSize = sizeof(entry);
            if (Process32FirstW(snapshot, &entry)) {
                do {
                    if (entry.th32ProcessID == shellPid) {
                        shellProcess = entry.szExeFile;
                        break;
                    }
                } while (Process32NextW(snapshot, &entry));
            }
            CloseHandle(snapshot);
        }
    }
    bool explorerRunning = IsProcessRunningByName(L"explorer.exe");
    bool steamRunning = IsProcessRunningByName(L"steam.exe");

    const char* sessionKind =
        (explorerRunning && _wcsicmp(shellProcess.c_str(), L"explorer.exe") == 0)
            ? "desktop-like"
            : "alternate-shell";

    logging::Logf(
        "[LaunchContext] exe=%s shellHwnd=%p shellPid=%lu shellProcess=%s shellClass=%s explorerRunning=%d steamRunning=%d inferredSession=%s",
        logging::Narrow(exePath).c_str(),
        shellWindow,
        static_cast<unsigned long>(shellPid),
        logging::Narrow(shellProcess).c_str(),
        logging::Narrow(shellClass).c_str(),
        explorerRunning ? 1 : 0,
        steamRunning ? 1 : 0,
        sessionKind);
}

TrayApp::TrayApp() {
    g_app = this;
}

TrayApp::~TrayApp() {
    RemoveTrayIcon();
    g_app = nullptr;
}

bool TrayApp::Init(HINSTANCE hInstance) {
    m_hInstance = hInstance;
    m_iconOff   = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON_OFF));
    m_iconOn    = LoadIconW(hInstance, MAKEINTRESOURCEW(IDI_ICON_ON));
    m_wmTaskbar = RegisterWindowMessageW(L"TaskbarCreated");
    LogLaunchContext();

    WNDCLASSEXW wc{};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInstance;
    wc.lpszClassName = WNDCLASS_NAME;
    if (!RegisterClassExW(&wc)) return false;

    // Message-only window — invisible, never shown.
    m_hwnd = CreateWindowExW(0, WNDCLASS_NAME, APP_NAME,
                             0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hInstance, nullptr);
    if (!m_hwnd) return false;

    // Register for HID device arrival/removal notifications.
    DEV_BROADCAST_DEVICEINTERFACE_W filter{};
    filter.dbcc_size       = sizeof(filter);
    filter.dbcc_devicetype = DBT_DEVTYP_DEVICEINTERFACE;
    // HID device interface GUID
    filter.dbcc_classguid  = {0x4D1E55B2, 0xF16F, 0x11CF,
                              {0x88, 0xCB, 0x00, 0x11, 0x11, 0x00, 0x00, 0x30}};
    RegisterDeviceNotificationW(m_hwnd, &filter, DEVICE_NOTIFY_WINDOW_HANDLE);

    m_controller = std::make_unique<ControllerManager>(
        [this](bool connected, bool gameModeActive, bool vigemMissing) {
            UpdateTrayIcon(connected, gameModeActive, vigemMissing);
        });
    m_ipcServer = std::make_unique<RemapIpcServer>(
        [this](const std::string& request) { return HandleIpcRequest(request); });

    m_steamRunning = IsSteamRunning();
    LoadSettings();
    LoadPaddleConfig();
    ApplyProfileById(m_remapBackend.GetActiveProfileId(), true);
    if ((HasRunEntry(LEGACY_APP_NAME) || HasRunEntry(OLD_APP_NAME)) && !HasRunEntry(APP_NAME))
        SetStartupEnabled(true);
    AddTrayIcon();
    UpdateTrayIcon(m_controller->IsConnected(), m_controller->IsGameModeActive(), false);
    SetTimer(m_hwnd, TIMER_STEAM_POLL, STEAM_POLL_MS, nullptr);
    m_ipcServer->Start();
    PublishWidgetState();
    ReconcileAutoMode();
    return true;
}

int TrayApp::Run() {
    MSG msg;
    BOOL ret;
    while ((ret = GetMessageW(&msg, nullptr, 0, 0)) != 0) {
        if (ret == -1) return -1;
        if (m_paddleConfigWindow && m_paddleConfigWindow->IsOpen()) {
            HWND paddleWindow = m_paddleConfigWindow->GetHwnd();
            if (paddleWindow && IsDialogMessageW(paddleWindow, &msg))
                continue;
        }
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return static_cast<int>(msg.wParam);
}

LRESULT CALLBACK TrayApp::WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (g_app) return g_app->HandleMessage(hwnd, msg, wp, lp);
    return DefWindowProcW(hwnd, msg, wp, lp);
}

LRESULT TrayApp::HandleMessage(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    if (msg == m_wmTaskbar) {
        AddTrayIcon();
        return 0;
    }

    switch (msg) {
    case WM_TRAY:
        if (LOWORD(lp) == NIN_BALLOONUSERCLICK)
            ShellExecuteW(nullptr, L"open", L"https://github.com/nefarius/ViGEmBus/releases/latest",
                          nullptr, nullptr, SW_SHOWNORMAL);
        else if (LOWORD(lp) == WM_RBUTTONUP || LOWORD(lp) == WM_LBUTTONUP)
            ShowContextMenu();
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_TOGGLE:
            if (m_controller->IsGameModeActive())
                m_controller->DisableGameMode();
            else if (!m_steamRunning)
                m_controller->EnableGameMode();
            break;
        case IDM_TRACKPAD:
            m_controller->SetTrackpadMouseEnabled(!m_controller->IsTrackpadMouseEnabled());
            SaveSettings();
            break;
        case IDM_LEFT_TRACKPAD:
            m_controller->SetUseLeftTrackpad(!m_controller->IsUseLeftTrackpad());
            SaveSettings();
            break;
        case IDM_STARTUP:
            SetStartupEnabled(!IsStartupEnabled());
            break;
        case IDM_AUTOENABLE:
            m_autoEnableSteamlessMode = !m_autoEnableSteamlessMode;
            SaveSettings();
            ReconcileAutoMode();
            break;
        case IDM_OUTPUT_X360:
            m_controller->SetEmulationMode(EmulationMode::Xbox360);
            SaveSettings();
            ReconcileAutoMode();
            break;
        case IDM_OUTPUT_DS4:
            m_controller->SetEmulationMode(EmulationMode::DualShock4);
            SaveSettings();
            ReconcileAutoMode();
            break;
        case IDM_CONFIGURE_PADDLES:
            if (!m_steamRunning)
                ShowPaddleConfigWindow();
            break;
        case IDM_EXIT:
            m_controller->DisableGameMode();
            PostQuitMessage(0);
            break;
        }
        return 0;

    case WM_DEVICECHANGE:
        if (wp == DBT_DEVICEARRIVAL || wp == DBT_DEVICEREMOVECOMPLETE) {
            m_lastReconnectAttemptTick = GetTickCount64();
            m_controller->OnDeviceChange();
            ReconcileAutoMode();
        }
        return TRUE;

    case WM_POWERBROADCAST:
        switch (wp) {
        case PBT_APMSUSPEND:
            m_controller->OnSuspend();
            return TRUE;
        case PBT_APMRESUMEAUTOMATIC:
        case PBT_APMRESUMESUSPEND:
            m_controller->OnResume();
            ReconcileAutoMode();
            return TRUE;
        }
        break;

    case WM_TIMER:
        if (wp == TIMER_STEAM_POLL) {
            // Retry controller discovery on the timer as well. On cold boot the
            // first open attempt can race HID initialization, and there may be
            // no later arrival event if the controller was already present.
            const ULONGLONG now = GetTickCount64();
            if (m_controller->IsConnected() ||
                (now - m_lastReconnectAttemptTick) >= RECONNECT_BACKOFF_MS) {
                m_lastReconnectAttemptTick = now;
                m_controller->OnDeviceChange();
                ReconcileAutoMode();
            }

            bool steamRunning = IsSteamRunning();
            if (steamRunning != m_steamRunning) {
                m_steamRunning = steamRunning;
                if (m_steamRunning && m_paddleConfigWindow)
                    m_paddleConfigWindow->Close();
                ReconcileAutoMode();
            }

            if (m_autoSwitchProfiles) {
                const std::wstring detectedProfileId = GetDetectedGameProfileId();
                if (!detectedProfileId.empty()) {
                    m_manualProfileOverride = false;
                    ApplyProfileById(detectedProfileId);
                } else if (!m_manualProfileOverride) {
                    ApplyProfileById(L"default");
                }
            }

            ProcessWidgetBridge();
            PublishWidgetState();
            return 0;
        }
        break;

    case WM_DESTROY:
        KillTimer(hwnd, TIMER_STEAM_POLL);
        if (m_ipcServer)
            m_ipcServer->Stop();
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void TrayApp::AddTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = m_hwnd;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    nid.uCallbackMessage = WM_TRAY;
    nid.hIcon            = m_iconOff;
    wcscpy_s(nid.szTip, APP_NAME);
    Shell_NotifyIconW(NIM_ADD, &nid);
    nid.uVersion = NOTIFYICON_VERSION_4;
    Shell_NotifyIconW(NIM_SETVERSION, &nid);
}

void TrayApp::RemoveTrayIcon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = m_hwnd;
    nid.uID    = TRAY_UID;
    Shell_NotifyIconW(NIM_DELETE, &nid);
}

void TrayApp::UpdateTrayIcon(bool connected, bool gameModeActive, bool vigemMissing) {
    if (vigemMissing) { ShowViGEmBalloon(); return; }
    bool gameModeOn = gameModeActive;

    const wchar_t* tip = gameModeOn  ? L"Steam Controller Remapper - Steamless Mode ON"
                       : connected   ? L"Steam Controller Remapper - Connected (Steamless Mode OFF)"
                                     : L"Steam Controller Remapper - No controller found";

    NOTIFYICONDATAW nid{};
    nid.cbSize = sizeof(nid);
    nid.hWnd   = m_hwnd;
    nid.uID    = TRAY_UID;
    nid.uFlags = NIF_TIP | NIF_ICON;
    nid.hIcon  = gameModeOn ? m_iconOn : m_iconOff;
    wcscpy_s(nid.szTip, tip);
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

void TrayApp::ShowViGEmBalloon() {
    NOTIFYICONDATAW nid{};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = m_hwnd;
    nid.uID              = TRAY_UID;
    nid.uFlags           = NIF_INFO;
    nid.dwInfoFlags      = NIIF_WARNING;
    wcscpy_s(nid.szInfoTitle, L"Driver required");
    wcscpy_s(nid.szInfo,      L"ViGEmBus is not installed. Click here to download it.");
    Shell_NotifyIconW(NIM_MODIFY, &nid);
}

bool TrayApp::IsSteamRunning() const {
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return false;

    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool running = false;

    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"steam.exe") == 0) {
                running = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }

    CloseHandle(snapshot);
    return running;
}

bool TrayApp::GetAutoSwitchProfiles() const {
    return m_autoSwitchProfiles;
}

void TrayApp::SetAutoSwitchProfiles(bool enabled) {
    m_autoSwitchProfiles = enabled;
    if (!enabled)
        m_manualProfileOverride = false;
    SaveSettings();
    PublishWidgetState();
}

std::wstring TrayApp::GetDetectedGameProfileId() const {
    std::vector<std::wstring> candidatePaths;
    HWND foreground = GetForegroundWindow();
    if (foreground) {
        DWORD pid = 0;
        GetWindowThreadProcessId(foreground, &pid);
        if (pid != 0 && pid != GetCurrentProcessId()) {
            const std::wstring processPath = GetProcessPathById(pid);
            if (!processPath.empty())
                candidatePaths.push_back(processPath);
        }
    }

    const std::wstring foregroundMatch =
        PaddleConfig::NormalizeProfileId(SteamLibrary::MatchProcessListToInstalledGame(candidatePaths));
    if (!foregroundMatch.empty() && foregroundMatch != L"default") {
        logging::Logf("[Profiles] Detected foreground profile id=%s",
                      logging::Narrow(foregroundMatch).c_str());
        return foregroundMatch;
    }

    if (m_remapBackend.GetActiveProfileId() != L"default") {
        const std::wstring runningMatch =
            PaddleConfig::NormalizeProfileId(m_remapBackend.MatchProcessListToInstalledGame(GetRunningProcessPaths()));
        if (runningMatch == m_remapBackend.GetActiveProfileId()) {
            logging::Logf("[Profiles] Keeping active running profile id=%s",
                          logging::Narrow(runningMatch).c_str());
            return runningMatch;
        }
    }

    return {};
}

void TrayApp::ApplyProfileById(const std::wstring& profileId, bool force) {
    if (!m_remapBackend.SetActiveProfileId(profileId, force))
        return;

    const RemapProfile* profile = m_remapBackend.GetActiveProfile();
    if (!profile)
        return;

    m_controller->SetPaddleMapping(0, profile->mappings.l4);
    m_controller->SetPaddleMapping(1, profile->mappings.l5);
    m_controller->SetPaddleMapping(2, profile->mappings.r4);
    m_controller->SetPaddleMapping(3, profile->mappings.r5);
    m_controller->SetPaddleMapping(4, profile->mappings.qam);
    m_controller->SetPaddleActions(profile->actions);
    SaveSettings();
    if (m_paddleConfigWindow && m_paddleConfigWindow->IsOpen())
        m_paddleConfigWindow->ReloadFromModel();
    logging::Logf("[Profiles] Applied profile id=%s",
                  logging::Narrow(m_remapBackend.GetActiveProfileId()).c_str());
    PublishWidgetState();
}

void TrayApp::ReconcileAutoMode() {
    if (!m_controller)
        return;

    bool shouldAutoEnable = m_autoEnableSteamlessMode &&
                            m_controller->IsConnected() &&
                            !m_steamRunning;

    if (shouldAutoEnable && !m_controller->IsGameModeActive()) {
        m_controller->EnableGameMode();
    } else if (m_steamRunning && m_controller->IsGameModeActive()) {
        m_controller->DisableGameMode();
    }
}

bool TrayApp::IsStartupEnabled() const {
    return HasAnyRunEntry();
}

void TrayApp::SetStartupEnabled(bool enabled) {
    HKEY key;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, REG_RUN_KEY, 0, KEY_WRITE, &key) != ERROR_SUCCESS)
        return;

    if (enabled) {
        wchar_t path[MAX_PATH];
        GetModuleFileNameW(nullptr, path, MAX_PATH);
        std::wstring command = L"\"";
        command += path;
        command += L"\"";
        RegSetValueExW(key, APP_NAME, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(command.c_str()),
                       static_cast<DWORD>((command.size() + 1) * sizeof(wchar_t)));
        RegDeleteValueW(key, LEGACY_APP_NAME);
        RegDeleteValueW(key, OLD_APP_NAME);
    } else {
        RegDeleteValueW(key, APP_NAME);
        RegDeleteValueW(key, LEGACY_APP_NAME);
        RegDeleteValueW(key, OLD_APP_NAME);
    }

    RegCloseKey(key);
}

void TrayApp::LoadSettings() {
    HKEY key;
    if (!OpenSettingsKeyForRead(key))
        return;

    auto readBool = [&](const wchar_t* name, bool def) -> bool {
        DWORD val = 0, size = sizeof(val);
        if (RegQueryValueExW(key, name, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS)
            return val != 0;
        return def;
    };
    auto readDword = [&](const wchar_t* name, DWORD def) -> DWORD {
        DWORD val = 0, size = sizeof(val);
        if (RegQueryValueExW(key, name, nullptr, nullptr,
                             reinterpret_cast<LPBYTE>(&val), &size) == ERROR_SUCCESS)
            return val;
        return def;
    };
    m_controller->SetTrackpadMouseEnabled(readBool(L"TrackpadMouse",   true));
    m_controller->SetBackButtonsEnabled  (readBool(L"BackButtons",     false));
    m_controller->SetUseLeftTrackpad     (readBool(L"UseLeftTrackpad", false));
    m_autoEnableSteamlessMode            = readBool(L"AutoEnableSteamlessMode", true);
    m_autoSwitchProfiles                 = readBool(L"AutoSwitchProfiles", false);
    m_controller->SetEmulationMode(readBool(L"UseDs4Emulation", false)
        ? EmulationMode::DualShock4
        : EmulationMode::Xbox360);
    m_controller->SetPaddleMapping(0, static_cast<PaddleMapping>(readDword(L"PaddleMapL4", 0)));
    m_controller->SetPaddleMapping(1, static_cast<PaddleMapping>(readDword(L"PaddleMapL5", 0)));
    m_controller->SetPaddleMapping(2, static_cast<PaddleMapping>(readDword(L"PaddleMapR4", 0)));
    m_controller->SetPaddleMapping(3, static_cast<PaddleMapping>(readDword(L"PaddleMapR5", 0)));
    m_controller->SetPaddleMapping(4, static_cast<PaddleMapping>(readDword(L"PaddleMapQAM", 0)));
    m_manualProfileOverride = false;

    RegCloseKey(key);
}

void TrayApp::SaveSettings() {
    HKEY key;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, REG_KEY, 0, nullptr,
                        REG_OPTION_NON_VOLATILE, KEY_WRITE, nullptr,
                        &key, nullptr) != ERROR_SUCCESS)
        return;

    auto writeBool = [&](const wchar_t* name, bool val) {
        DWORD dw = val ? 1 : 0;
        RegSetValueExW(key, name, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&dw), sizeof(dw));
    };
    auto writeDword = [&](const wchar_t* name, DWORD val) {
        RegSetValueExW(key, name, 0, REG_DWORD,
                       reinterpret_cast<const BYTE*>(&val), sizeof(val));
    };
    auto writeString = [&](const wchar_t* name, const std::wstring& value) {
        RegSetValueExW(key, name, 0, REG_SZ,
                       reinterpret_cast<const BYTE*>(value.c_str()),
                       static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t)));
    };
    const PaddleMappings paddles = m_controller->GetPaddleMappings();

    writeBool(L"TrackpadMouse",   m_controller->IsTrackpadMouseEnabled());
    writeBool(L"BackButtons",     m_controller->IsBackButtonsEnabled());
    writeBool(L"UseLeftTrackpad", m_controller->IsUseLeftTrackpad());
    writeBool(L"AutoEnableSteamlessMode", m_autoEnableSteamlessMode);
    writeBool(L"AutoSwitchProfiles", m_autoSwitchProfiles);
    writeBool(L"UseDs4Emulation",
              m_controller->GetEmulationMode() == EmulationMode::DualShock4);
    writeDword(L"PaddleMapL4", static_cast<DWORD>(paddles.l4));
    writeDword(L"PaddleMapL5", static_cast<DWORD>(paddles.l5));
    writeDword(L"PaddleMapR4", static_cast<DWORD>(paddles.r4));
    writeDword(L"PaddleMapR5", static_cast<DWORD>(paddles.r5));
    writeDword(L"PaddleMapQAM", static_cast<DWORD>(paddles.qam));
    writeString(REG_LAST_PROFILE, m_remapBackend.GetActiveProfileId());
    writeBool(REG_MANUAL_OVERRIDE, m_manualProfileOverride);

    RegCloseKey(key);
}

void TrayApp::LoadPaddleConfig() {
    PaddleConfig::EnsureExists();
    PaddleActionBindings legacyActions = PaddleConfig::Load();
    PaddleMappings legacyMappings = m_controller->GetPaddleMappings();
    m_remapBackend.Load(legacyMappings, legacyActions);
}

void TrayApp::ShowPaddleConfigWindow() {
    if (!m_paddleConfigWindow) {
        m_paddleConfigWindow = std::make_unique<PaddleConfigWindow>(
            m_remapBackend,
            [this]() { return m_controller->GetCurrentMacroCaptureChord(); },
            [this]() { return m_controller->GetUiNavigationState(); },
            [this]() { return GetAutoSwitchProfiles(); },
            [this](bool enabled) { SetAutoSwitchProfiles(enabled); },
            [this](const std::wstring& profileId, bool force) {
                m_manualProfileOverride = true;
                ApplyProfileById(profileId, force);
            });
    }

    m_paddleConfigWindow->Show(m_hInstance, m_hwnd);
}

std::string TrayApp::HandleIpcRequest(const std::string& request) {
    const std::string trimmed = TrimAscii(request);
    if (trimmed.empty())
        return "ERR\tempty-request";

    size_t split = trimmed.find('\t');
    if (split == std::string::npos)
        split = trimmed.find(' ');
    const std::string command = split == std::string::npos ? trimmed : trimmed.substr(0, split);
    const std::string payload = split == std::string::npos ? std::string{} : TrimAscii(trimmed.substr(split + 1));

    if (command == "GET_STATE") {
        const std::wstring activeProfileId = m_remapBackend.GetActiveProfileId();
        const std::wstring detectedProfileId = GetDetectedGameProfileId();
        std::ostringstream json;
        json << "{\"activeProfileId\":\"" << JsonEscape(activeProfileId)
             << "\",\"detectedProfileId\":\"" << JsonEscape(detectedProfileId)
             << "\",\"autoSwitchProfiles\":" << JsonBool(m_autoSwitchProfiles)
             << ",\"steamRunning\":" << JsonBool(m_steamRunning)
             << "}";
        return "OK\t" + json.str();
    }

    if (command == "LIST_GAMES") {
        const std::vector<std::wstring> games = m_remapBackend.GetInstalledGames();
        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < games.size(); ++i) {
            if (i != 0)
                json << ",";
            json << "\"" << JsonEscape(games[i]) << "\"";
        }
        json << "]";
        return "OK\t" + json.str();
    }

    if (command == "APPLY_PROFILE") {
        const std::wstring profileId = WidenUtf8(payload);
        if (profileId.empty())
            return "ERR\tmissing-profile-id";
        m_manualProfileOverride = true;
        ApplyProfileById(profileId, true);
        return "OK\t{\"activeProfileId\":\"" + JsonEscape(m_remapBackend.GetActiveProfileId()) + "\"}";
    }

    if (command == "GET_PROFILE") {
        const std::wstring profileId = payload.empty() ? m_remapBackend.GetActiveProfileId() : WidenUtf8(payload);
        const RemapProfile* profile = m_remapBackend.GetProfileById(profileId);
        if (!profile)
            return "ERR\tprofile-not-found";

        auto describe = [&](const PaddleAction& action, PaddleMapping fallback) {
            return JsonEscape(PaddleConfig::Describe(action, fallback));
        };

        std::ostringstream json;
        json << "{"
             << "\"id\":\"" << JsonEscape(profile->id) << "\","
             << "\"l4\":\"" << describe(profile->actions.l4, profile->mappings.l4) << "\","
             << "\"l5\":\"" << describe(profile->actions.l5, profile->mappings.l5) << "\","
             << "\"r4\":\"" << describe(profile->actions.r4, profile->mappings.r4) << "\","
             << "\"r5\":\"" << describe(profile->actions.r5, profile->mappings.r5) << "\","
             << "\"qam\":\"" << describe(profile->actions.qam, profile->mappings.qam) << "\""
             << "}";
        return "OK\t" + json.str();
    }

    if (command == "SET_AUTO_SWITCH") {
        if (payload != "0" && payload != "1")
            return "ERR\tinvalid-auto-switch";
        SetAutoSwitchProfiles(payload == "1");
        return "OK\t{\"autoSwitchProfiles\":" + JsonBool(m_autoSwitchProfiles) + "}";
    }

    if (command == "OPEN_DESKTOP_EDITOR") {
        if (m_steamRunning)
            return "ERR\tsteam-running";
        logging::Logf("[WidgetBridge] OPEN_DESKTOP_EDITOR begin");
        ShowPaddleConfigWindow();
        if (m_paddleConfigWindow && m_paddleConfigWindow->GetHwnd()) {
            HWND editor = m_paddleConfigWindow->GetHwnd();
            ShowWindow(editor, SW_SHOWNORMAL);
            BringWindowToTop(editor);
            SetForegroundWindow(editor);
            SetFocus(editor);
            logging::Logf("[WidgetBridge] OPEN_DESKTOP_EDITOR focused hwnd=%p", editor);
            DismissGameBarAndRefocus(editor);
        }
        return "OK\t{\"desktopEditorOpened\":true}";
    }

    if (command == "SET_PROFILE_GAMEPAD_BINDING") {
        std::string profileToken;
        std::string paddleToken;
        std::string mappingToken;
        std::istringstream payloadStream(payload);
        std::getline(payloadStream, profileToken, '\t');
        std::getline(payloadStream, paddleToken, '\t');
        std::getline(payloadStream, mappingToken);

        const std::wstring profileId = WidenUtf8(TrimAscii(profileToken));
        const std::wstring paddleId = WidenUtf8(TrimAscii(paddleToken));
        const std::wstring mappingId = WidenUtf8(TrimAscii(mappingToken));
        if (profileId.empty() || paddleId.empty() || mappingId.empty())
            return "ERR\tinvalid-binding-payload";

        PaddleMapping mapping = PaddleMapping::None;
        if (!TryParseMappingToken(mappingId, mapping))
            return "ERR\tinvalid-binding-mapping";

        RemapProfile* profile = m_remapBackend.EnsureProfileExists(profileId);
        if (!profile)
            return "ERR\tprofile-not-found";

        PaddleAction* action = GetPaddleAction(profile->actions, paddleId);
        PaddleMapping* fallback = GetPaddleMapping(profile->mappings, paddleId);
        if (!action || !fallback)
            return "ERR\tinvalid-paddle-id";

        *fallback = mapping;
        action->type = mapping == PaddleMapping::None ? PaddleActionType::None : PaddleActionType::Gamepad;
        action->gamepadMapping = mapping;
        action->chord.clear();
        action->macroSteps.clear();
        action->rapidFire = false;
        m_remapBackend.PersistProfiles();

        const std::wstring normalizedProfileId = PaddleConfig::NormalizeProfileId(profileId);
        if (m_remapBackend.GetActiveProfileId() == normalizedProfileId)
            ApplyProfileById(normalizedProfileId, true);

        std::ostringstream json;
        json << "{"
             << "\"profileId\":\"" << JsonEscape(normalizedProfileId) << "\","
             << "\"paddle\":\"" << JsonEscape(paddleId) << "\","
             << "\"mappingToken\":\"" << MappingToken(mapping) << "\""
             << "}";
        return "OK\t" + json.str();
    }

    return "ERR\tunknown-command";
}

void TrayApp::PublishWidgetState() {
    const std::wstring widgetDir = GetWidgetLocalStateDirectory();
    if (widgetDir.empty()) {
        static bool loggedMissingDir = false;
        if (!loggedMissingDir) {
            logging::Logf("[WidgetBridge] No widget LocalState directory found");
            loggedMissingDir = true;
        }
        return;
    }

    static std::wstring lastLoggedWidgetDir;
    if (widgetDir != lastLoggedWidgetDir) {
        logging::Logf("[WidgetBridge] Publishing to %s", logging::Narrow(widgetDir).c_str());
        lastLoggedWidgetDir = widgetDir;
    }

    CreateDirectoryW(widgetDir.c_str(), nullptr);

    const std::vector<std::wstring> games = m_remapBackend.GetInstalledGames();
    const std::wstring activeProfileId = m_remapBackend.GetActiveProfileId();
    const std::wstring detectedProfileId = GetDetectedGameProfileId();
    const RemapProfile* profile = m_remapBackend.GetActiveProfile();

    std::ostringstream json;
    json << "{"
         << "\"activeProfileId\":\"" << JsonEscape(activeProfileId) << "\","
         << "\"detectedProfileId\":\"" << JsonEscape(detectedProfileId) << "\","
         << "\"autoSwitchProfiles\":" << JsonBool(m_autoSwitchProfiles) << ","
         << "\"steamRunning\":" << JsonBool(m_steamRunning) << ","
         << "\"installedGames\":[";
    for (size_t i = 0; i < games.size(); ++i) {
        if (i != 0)
            json << ",";
        json << "\"" << JsonEscape(games[i]) << "\"";
    }
    json << "]";
    if (profile) {
        json << ",\"profile\":{"
             << "\"id\":\"" << JsonEscape(profile->id) << "\",";
        AppendBindingJson(json, "l4", profile->actions.l4, profile->mappings.l4);
        json << ",";
        AppendBindingJson(json, "l5", profile->actions.l5, profile->mappings.l5);
        json << ",";
        AppendBindingJson(json, "r4", profile->actions.r4, profile->mappings.r4);
        json << ",";
        AppendBindingJson(json, "r5", profile->actions.r5, profile->mappings.r5);
        json << ",";
        AppendBindingJson(json, "qam", profile->actions.qam, profile->mappings.qam);
        json << "}";
    }
    json << "}";

    const std::string text = json.str();
    if (text == m_lastWidgetStateJson)
        return;

    if (WriteUtf8File(widgetDir + L"\\widget-state.json", text)) {
        m_lastWidgetStateJson = text;
    } else {
        logging::Logf("[WidgetBridge] Failed writing widget-state.json");
    }
}

void TrayApp::ProcessWidgetBridge() {
    const std::wstring widgetDir = GetWidgetLocalStateDirectory();
    if (widgetDir.empty())
        return;

    const std::vector<std::string> lines = SplitLines(ReadUtf8File(widgetDir + L"\\widget-request.txt"));
    if (lines.size() < 2)
        return;

    const std::string requestId = TrimAscii(lines[0]);
    const std::string command = TrimAscii(lines[1]);
    const std::string payload = lines.size() >= 3 ? TrimAscii(lines[2]) : std::string{};

    if (requestId.empty() || requestId == m_lastWidgetRequestId)
        return;

    logging::Logf("[WidgetBridge] Processing request id=%s command=%s payload=%s",
                  requestId.c_str(), command.c_str(), payload.c_str());

    std::string response = "ERR\tunknown-command";
    if (command == "APPLY_PROFILE") {
        response = HandleIpcRequest("APPLY_PROFILE\t" + payload);
    } else if (command == "SET_AUTO_SWITCH") {
        response = HandleIpcRequest("SET_AUTO_SWITCH\t" + payload);
    } else if (command == "REFRESH_LIBRARY") {
        m_remapBackend.RefreshInstalledGames();
        response = "OK\t{\"refreshed\":true}";
    } else if (command == "OPEN_DESKTOP_EDITOR") {
        response = HandleIpcRequest("OPEN_DESKTOP_EDITOR");
    } else if (command == "SET_PROFILE_GAMEPAD_BINDING") {
        response = HandleIpcRequest("SET_PROFILE_GAMEPAD_BINDING\t" + payload);
    }

    std::ostringstream out;
    out << requestId << "\n";
    if (response.rfind("OK\t", 0) == 0) {
        out << "OK\n" << response.substr(3) << "\n";
    } else if (response.rfind("ERR\t", 0) == 0) {
        out << "ERR\n" << response.substr(4) << "\n";
    } else {
        out << "ERR\n" << response << "\n";
    }

    if (WriteUtf8File(widgetDir + L"\\widget-response.txt", out.str()))
        m_lastWidgetRequestId = requestId;
    else
        logging::Logf("[WidgetBridge] Failed writing widget-response.txt");
}

void TrayApp::ShowContextMenu() {
    bool connected      = m_controller->IsConnected();
    bool gameModeOn     = m_controller->IsGameModeActive();
    bool trackpadOn     = m_controller->IsTrackpadMouseEnabled();
    bool leftTrackpad   = m_controller->IsUseLeftTrackpad();
    bool startupOn      = IsStartupEnabled();
    bool canEnableMode  = connected && !m_steamRunning;
    bool canConfigure   = !m_steamRunning;
    bool ds4Mode        = m_controller->GetEmulationMode() == EmulationMode::DualShock4;

    HMENU menu = CreatePopupMenu();

    UINT toggleFlags = MF_STRING | ((gameModeOn || canEnableMode) ? MF_ENABLED : MF_GRAYED);
    AppendMenuW(menu, toggleFlags, IDM_TOGGLE,
                gameModeOn ? L"Disable Steamless Mode" : L"Enable Steamless Mode");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    UINT autoEnableFlags = MF_STRING | (m_autoEnableSteamlessMode ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, autoEnableFlags, IDM_AUTOENABLE, L"Auto-enable Steamless Mode");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    UINT x360Flags = MF_STRING | (ds4Mode ? MF_UNCHECKED : MF_CHECKED);
    AppendMenuW(menu, x360Flags, IDM_OUTPUT_X360, L"Emulate Xbox 360 Controller");

    UINT ds4Flags = MF_STRING | (ds4Mode ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, ds4Flags, IDM_OUTPUT_DS4, L"Emulate DualShock 4");
    AppendMenuW(menu, MF_STRING | (canConfigure ? MF_ENABLED : MF_GRAYED), IDM_CONFIGURE_PADDLES, L"Remap Buttons...");

    UINT trackpadFlags = MF_STRING | (trackpadOn ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, trackpadFlags, IDM_TRACKPAD, L"Enable Trackpad Mouse");

    UINT leftFlags = MF_STRING | (leftTrackpad ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, leftFlags, IDM_LEFT_TRACKPAD, L"Use Left Trackpad Instead");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);

    UINT startupFlags = MF_STRING | (startupOn ? MF_CHECKED : MF_UNCHECKED);
    AppendMenuW(menu, startupFlags, IDM_STARTUP, L"Start with Windows");

    AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(menu, MF_STRING, IDM_EXIT, L"Exit");

    // SetForegroundWindow is required for the menu to dismiss on click-away.
    SetForegroundWindow(m_hwnd);

    POINT pt;
    GetCursorPos(&pt);
    TrackPopupMenu(menu, TPM_RIGHTALIGN | TPM_BOTTOMALIGN | TPM_RIGHTBUTTON,
                   pt.x, pt.y, 0, m_hwnd, nullptr);
    DestroyMenu(menu);
}
