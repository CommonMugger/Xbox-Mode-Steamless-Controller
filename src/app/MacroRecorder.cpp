#include "MacroRecorder.h"
#include <algorithm>
#include <cwctype>
#include <sstream>
#include <set>
#include <string>
#include <vector>

namespace {
constexpr wchar_t kClassName[] = L"SteamControllerRemapperMacroRecorder";
constexpr int IDC_LIST = 3002;
constexpr int IDC_HINT = 3003;
constexpr int IDC_EDIT_STEP = 3004;
constexpr int IDC_DELETE_STEP = 3005;
constexpr int IDC_CLEAR_ALL = 3006;
constexpr int IDC_CAPTURE = 3007;
constexpr int IDC_SAVE = 3008;
constexpr int IDC_CANCEL = 3009;
constexpr int IDC_STATUS = 3010;
constexpr UINT TIMER_CONTROLLER_POLL = 1;

bool IsModifier(WPARAM vk) {
    return vk == VK_CONTROL || vk == VK_LCONTROL || vk == VK_RCONTROL ||
           vk == VK_SHIFT || vk == VK_LSHIFT || vk == VK_RSHIFT ||
           vk == VK_MENU || vk == VK_LMENU || vk == VK_RMENU ||
           vk == VK_LWIN || vk == VK_RWIN;
}

bool IsControllerSaveChord(const std::wstring& chord) {
    return chord.find(L"MENU") != std::wstring::npos;
}

bool IsControllerCancelChord(const std::wstring& chord) {
    return chord.find(L"VIEW") != std::wstring::npos;
}

std::wstring VkName(UINT vk) {
    if (vk >= L'A' && vk <= L'Z') return std::wstring(1, static_cast<wchar_t>(vk));
    if (vk >= L'0' && vk <= L'9') return std::wstring(1, static_cast<wchar_t>(vk));
    switch (vk) {
    case VK_CONTROL:
    case VK_LCONTROL:
    case VK_RCONTROL: return L"CTRL";
    case VK_SHIFT:
    case VK_LSHIFT:
    case VK_RSHIFT: return L"SHIFT";
    case VK_MENU:
    case VK_LMENU:
    case VK_RMENU: return L"ALT";
    case VK_LWIN:
    case VK_RWIN: return L"WIN";
    case VK_TAB: return L"TAB";
    case VK_RETURN: return L"ENTER";
    case VK_SPACE: return L"SPACE";
    case VK_ESCAPE: return L"ESC";
    case VK_BACK: return L"BACKSPACE";
    case VK_UP: return L"UP";
    case VK_RIGHT: return L"RIGHT";
    case VK_DOWN: return L"DOWN";
    case VK_LEFT: return L"LEFT";
    default:
        if (vk >= VK_F1 && vk <= VK_F24)
            return L"F" + std::to_wstring((vk - VK_F1) + 1);
        return L"VK_" + std::to_wstring(vk);
    }
}

std::wstring JoinChord(const std::vector<UINT>& keys) {
    std::wstring text;
    for (size_t i = 0; i < keys.size(); ++i) {
        if (i != 0)
            text += L"+";
        text += VkName(keys[i]);
    }
    return text;
}

struct RecorderState {
    HWND hwnd = nullptr;
    HWND list = nullptr;
    HWND status = nullptr;
    HWND captureButton = nullptr;
    HWND saveButton = nullptr;
    HWND cancelButton = nullptr;
    bool accepted = false;
    std::set<UINT> held;
    std::vector<std::wstring> steps;
    std::wstring result;
    MacroRecorder::ControllerChordFn controllerChordFn;
    MacroRecorder::ControllerUiStateFn controllerUiStateFn;
    MacroRecorder::ControllerUiState lastControllerUiState{};
    std::wstring lastControllerChord;
    int appendIndex = -1;
    bool captureArmed = false;
    bool captureToSelected = false;
    bool waitingForChordRelease = false;
    int focusIndex = 0;
};

RecorderState* g_activeRecorder = nullptr;
HHOOK g_keyboardHook = nullptr;
HWND g_recorderWindow = nullptr;

void ArmControllerCapture(RecorderState* state, bool appendToSelected);

std::vector<std::wstring> SplitChordTokens(const std::wstring& text) {
    std::vector<std::wstring> tokens;
    std::wstringstream stream(text);
    std::wstring item;
    while (std::getline(stream, item, L'+')) {
        if (!item.empty())
            tokens.push_back(item);
    }
    return tokens;
}

void SetStatus(RecorderState* state, const std::wstring& text) {
    if (!state || !state->status)
        return;
    SetWindowTextW(state->status, text.c_str());
}

std::wstring Trim(std::wstring value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](wchar_t ch) {
        return !std::iswspace(ch);
    }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](wchar_t ch) {
        return !std::iswspace(ch);
    }).base(), value.end());
    return value;
}

void RenderList(RecorderState* state) {
    SendMessageW(state->list, LB_RESETCONTENT, 0, 0);
    HDC hdc = GetDC(state->list);
    SIZE maxExtent{};
    for (size_t i = 0; i < state->steps.size(); ++i) {
        std::wstring item = state->steps[i];
        if (static_cast<int>(i) == state->appendIndex)
            item = L"[Append] " + item;
        SendMessageW(state->list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item.c_str()));
        if (hdc) {
            SIZE itemSize{};
            if (GetTextExtentPoint32W(hdc, item.c_str(), static_cast<int>(item.size()), &itemSize) && itemSize.cx > maxExtent.cx)
                maxExtent = itemSize;
        }
    }
    if (hdc)
        ReleaseDC(state->list, hdc);
    SendMessageW(state->list, LB_SETHORIZONTALEXTENT, maxExtent.cx + 24, 0);

    if (!state->steps.empty()) {
        const int selected = (state->appendIndex >= 0 && state->appendIndex < static_cast<int>(state->steps.size()))
            ? state->appendIndex
            : static_cast<int>(state->steps.size() - 1);
        SendMessageW(state->list, LB_SETCURSEL, static_cast<WPARAM>(selected), 0);
    }
}

void RefreshListItem(RecorderState* state, int index) {
    if (index < 0 || index >= static_cast<int>(state->steps.size())) return;
    RenderList(state);
    SendMessageW(state->list, LB_SETCURSEL, static_cast<WPARAM>(index), 0);
}

std::wstring MergeSteps(const std::wstring& existing, const std::wstring& extra) {
    std::vector<std::wstring> merged = SplitChordTokens(existing);
    for (const std::wstring& token : SplitChordTokens(extra)) {
        if (std::find(merged.begin(), merged.end(), token) == merged.end())
            merged.push_back(token);
    }

    std::wstring result;
    for (size_t i = 0; i < merged.size(); ++i) {
        if (i != 0)
            result += L"+";
        result += merged[i];
    }
    return result;
}

std::wstring RemoveLastToken(const std::wstring& chordText) {
    std::vector<std::wstring> tokens = SplitChordTokens(chordText);
    if (tokens.empty())
        return {};

    tokens.pop_back();
    std::wstring result;
    for (size_t i = 0; i < tokens.size(); ++i) {
        if (i != 0)
            result += L"+";
        result += tokens[i];
    }
    return result;
}

void AddOrMergeStep(RecorderState* state, const std::wstring& step) {
    if (step.empty())
        return;

    if (state->appendIndex >= 0 && state->appendIndex < static_cast<int>(state->steps.size())) {
        state->steps[state->appendIndex] = MergeSteps(state->steps[state->appendIndex], step);
        RefreshListItem(state, state->appendIndex);
        SetStatus(state, L"Updated step: " + state->steps[state->appendIndex]);
        state->appendIndex = -1;
        return;
    }

    state->steps.push_back(step);
    RenderList(state);
    SendMessageW(state->list, LB_SETCURSEL, static_cast<WPARAM>(state->steps.size() - 1), 0);
    SetStatus(state, step);
}

void DeleteSelectedStep(RecorderState* state) {
    int index = static_cast<int>(SendMessageW(state->list, LB_GETCURSEL, 0, 0));
    if (index == LB_ERR) {
        if (state->steps.empty())
            return;
        index = static_cast<int>(state->steps.size() - 1);
    }

    state->steps.erase(state->steps.begin() + index);
    if (state->appendIndex == index)
        state->appendIndex = -1;
    else if (state->appendIndex > index)
        --state->appendIndex;
    RenderList(state);

    if (state->steps.empty()) {
        state->appendIndex = -1;
        SetStatus(state, L"Recording macro input");
        return;
    }

    const int newIndex = (index >= static_cast<int>(state->steps.size()))
        ? static_cast<int>(state->steps.size() - 1)
        : index;
    SendMessageW(state->list, LB_SETCURSEL, static_cast<WPARAM>(newIndex), 0);
    SetStatus(state, L"Selected: " + state->steps[newIndex]);
}

void BeginEditSelectedStep(RecorderState* state) {
    const int index = static_cast<int>(SendMessageW(state->list, LB_GETCURSEL, 0, 0));
    if (index == LB_ERR || index >= static_cast<int>(state->steps.size()))
        return;

    ArmControllerCapture(state, true);
}

void ClearAllSteps(RecorderState* state) {
    state->held.clear();
    state->appendIndex = -1;
    state->captureArmed = false;
    state->captureToSelected = false;
    state->steps.clear();
    RenderList(state);
    SetStatus(state, L"No steps. Use Capture Step to add one.");
}

void RefreshRecorderFocus(RecorderState* state) {
    if (!state || !state->hwnd)
        return;
    const HWND order[] = {
        state->list,
        state->captureButton,
        GetDlgItem(state->hwnd, IDC_EDIT_STEP),
        GetDlgItem(state->hwnd, IDC_DELETE_STEP),
        GetDlgItem(state->hwnd, IDC_CLEAR_ALL),
        state->saveButton,
        state->cancelButton,
    };
    const int count = static_cast<int>(std::size(order));
    if (state->focusIndex < 0)
        state->focusIndex = 0;
    if (state->focusIndex >= count)
        state->focusIndex = count - 1;
    HWND target = order[state->focusIndex];
    if (target)
        SetFocus(target);
}

bool FocusRecorderControl(RecorderState* state, HWND target) {
    if (!state || !target)
        return false;
    const HWND order[] = {
        state->list,
        state->captureButton,
        GetDlgItem(state->hwnd, IDC_EDIT_STEP),
        GetDlgItem(state->hwnd, IDC_DELETE_STEP),
        GetDlgItem(state->hwnd, IDC_CLEAR_ALL),
        state->saveButton,
        state->cancelButton,
    };
    for (int i = 0; i < static_cast<int>(std::size(order)); ++i) {
        if (order[i] != target)
            continue;
        state->focusIndex = i;
        RefreshRecorderFocus(state);
        return true;
    }
    return false;
}

void MoveRecorderFocus(RecorderState* state, int delta) {
    if (!state)
        return;
    const HWND order[] = {
        state->list,
        state->captureButton,
        GetDlgItem(state->hwnd, IDC_EDIT_STEP),
        GetDlgItem(state->hwnd, IDC_DELETE_STEP),
        GetDlgItem(state->hwnd, IDC_CLEAR_ALL),
        state->saveButton,
        state->cancelButton,
    };
    const int count = static_cast<int>(std::size(order));
    const HWND current = GetFocus();
    for (int i = 0; i < count; ++i) {
        if (order[i] == current) {
            state->focusIndex = i;
            break;
        }
    }
    state->focusIndex += delta;
    if (state->focusIndex < 0)
        state->focusIndex = count - 1;
    if (state->focusIndex >= count)
        state->focusIndex = 0;
    RefreshRecorderFocus(state);
}

void SelectListDelta(RecorderState* state, int delta) {
    if (!state || state->steps.empty())
        return;
    int selected = static_cast<int>(SendMessageW(state->list, LB_GETCURSEL, 0, 0));
    if (selected == LB_ERR)
        selected = 0;
    selected += delta;
    if (selected < 0)
        selected = static_cast<int>(state->steps.size()) - 1;
    if (selected >= static_cast<int>(state->steps.size()))
        selected = 0;
    SendMessageW(state->list, LB_SETCURSEL, selected, 0);
    SetStatus(state, L"Selected: " + state->steps[selected]);
}

void ArmControllerCapture(RecorderState* state, bool appendToSelected) {
    if (!state)
        return;
    state->captureArmed = true;
    state->captureToSelected = appendToSelected;
    state->waitingForChordRelease = true;
    state->held.clear();
    if (appendToSelected) {
        const int index = static_cast<int>(SendMessageW(state->list, LB_GETCURSEL, 0, 0));
        if (index >= 0 && index < static_cast<int>(state->steps.size())) {
            state->appendIndex = index;
            RenderList(state);
            SendMessageW(state->list, LB_SETCURSEL, index, 0);
            SetStatus(state, L"Capture armed. Release buttons, then press the chord to append.");
            return;
        }
    }
    state->appendIndex = -1;
    RenderList(state);
    SetStatus(state, L"Capture armed. Release buttons, then press the chord to add a step.");
}

void BackspaceAction(RecorderState* state) {
    state->held.clear();
    if (state->appendIndex >= 0 && state->appendIndex < static_cast<int>(state->steps.size())) {
        std::wstring updated = RemoveLastToken(state->steps[state->appendIndex]);
        if (updated.empty()) {
            DeleteSelectedStep(state);
            return;
        }

        state->steps[state->appendIndex] = std::move(updated);
        RefreshListItem(state, state->appendIndex);
        SetStatus(state, L"Editing selected step");
        return;
    }

    DeleteSelectedStep(state);
}

void FinishRecorder(RecorderState* state, bool accepted) {
    state->accepted = accepted;
    state->result.clear();
    if (accepted) {
        for (size_t i = 0; i < state->steps.size(); ++i) {
            if (i != 0)
                state->result += L", ";
            state->result += state->steps[i];
        }
    }
    DestroyWindow(state->hwnd);
}

LRESULT CALLBACK KeyboardHookProc(int code, WPARAM wp, LPARAM lp) {
    if (code < 0 || !g_activeRecorder)
        return CallNextHookEx(g_keyboardHook, code, wp, lp);

    const KBDLLHOOKSTRUCT* key = reinterpret_cast<KBDLLHOOKSTRUCT*>(lp);
    const UINT vk = key->vkCode;

    if (wp == WM_KEYDOWN || wp == WM_SYSKEYDOWN) {
        if (vk == VK_ESCAPE) {
            FinishRecorder(g_activeRecorder, false);
            return 1;
        }
        if (vk == VK_RETURN) {
            FinishRecorder(g_activeRecorder, true);
            return 1;
        }
        if (vk == VK_BACK) {
            BackspaceAction(g_activeRecorder);
            return 1;
        }

        const bool inserted = g_activeRecorder->held.insert(vk).second;
        if (inserted && !IsModifier(vk)) {
            std::vector<UINT> chord(g_activeRecorder->held.begin(), g_activeRecorder->held.end());
            AddOrMergeStep(g_activeRecorder, JoinChord(chord));
            g_activeRecorder->held.clear();
        }
    } else if (wp == WM_KEYUP || wp == WM_SYSKEYUP) {
        if (IsModifier(vk) &&
            g_activeRecorder->held.size() == 1 &&
            g_activeRecorder->held.count(vk) != 0) {
            AddOrMergeStep(g_activeRecorder, JoinChord(std::vector<UINT>{vk}));
            g_activeRecorder->held.clear();
        }
        g_activeRecorder->held.erase(vk);
    }

    return 1;
}

LRESULT CALLBACK RecorderProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    RecorderState* state = reinterpret_cast<RecorderState*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    if (msg == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lp);
        state = reinterpret_cast<RecorderState*>(create->lpCreateParams);
        state->hwnd = hwnd;
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(state));
    }

    if (!state)
        return DefWindowProcW(hwnd, msg, wp, lp);

    switch (msg) {
    case WM_CREATE:
        state->list = CreateWindowW(L"LISTBOX", L"", WS_CHILD | WS_VISIBLE | WS_BORDER | LBS_NOTIFY | WS_VSCROLL | WS_HSCROLL,
                                    16, 16, 436, 196, hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_LIST))), nullptr, nullptr);
        state->captureButton = CreateWindowW(L"BUTTON", L"Capture Step", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                             16, 224, 132, 30, hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_CAPTURE))), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Append Selected", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                      160, 224, 132, 30, hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_EDIT_STEP))), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Delete Step", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                      304, 224, 148, 30, hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_DELETE_STEP))), nullptr, nullptr);
        CreateWindowW(L"BUTTON", L"Clear All", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                      16, 262, 132, 30, hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_CLEAR_ALL))), nullptr, nullptr);
        state->saveButton = CreateWindowW(L"BUTTON", L"Save", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                          160, 262, 132, 30, hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_SAVE))), nullptr, nullptr);
        state->cancelButton = CreateWindowW(L"BUTTON", L"Cancel", WS_CHILD | WS_VISIBLE | WS_TABSTOP,
                                            304, 262, 148, 30, hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_CANCEL))), nullptr, nullptr);
        state->status = CreateWindowW(L"STATIC", L"", WS_CHILD | WS_VISIBLE,
                                      16, 304, 436, 36, hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_STATUS))), nullptr, nullptr);
        CreateWindowW(L"STATIC",
                      L"D-pad moves. A activates. Capture Step records the next controller chord. Menu saves. View cancels.",
                      WS_CHILD | WS_VISIBLE,
                      16, 344, 436, 36, hwnd, static_cast<HMENU>(reinterpret_cast<void*>(static_cast<INT_PTR>(IDC_HINT))), nullptr, nullptr);
        RenderList(state);
        SetStatus(state, state->steps.empty()
            ? L"No steps. Use Capture Step to add one."
            : L"Select a step or use Capture Step to add another.");
        RefreshRecorderFocus(state);
        SetTimer(hwnd, TIMER_CONTROLLER_POLL, 30, nullptr);
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) == IDC_CAPTURE && HIWORD(wp) == BN_CLICKED) {
            ArmControllerCapture(state, false);
            return 0;
        }
        if (LOWORD(wp) == IDC_EDIT_STEP && HIWORD(wp) == BN_CLICKED) {
            BeginEditSelectedStep(state);
            return 0;
        }
        if (LOWORD(wp) == IDC_DELETE_STEP && HIWORD(wp) == BN_CLICKED) {
            DeleteSelectedStep(state);
            return 0;
        }
        if (LOWORD(wp) == IDC_CLEAR_ALL && HIWORD(wp) == BN_CLICKED) {
            ClearAllSteps(state);
            return 0;
        }
        if (LOWORD(wp) == IDC_SAVE && HIWORD(wp) == BN_CLICKED) {
            FinishRecorder(state, true);
            return 0;
        }
        if (LOWORD(wp) == IDC_CANCEL && HIWORD(wp) == BN_CLICKED) {
            FinishRecorder(state, false);
            return 0;
        }
        if (LOWORD(wp) == IDC_LIST && HIWORD(wp) == LBN_DBLCLK) {
            BeginEditSelectedStep(state);
            return 0;
        }
        break;
    case WM_TIMER:
        if (wp == TIMER_CONTROLLER_POLL) {
            if (state->controllerUiStateFn) {
                const auto current = state->controllerUiStateFn();
                auto pressed = [&](bool ControllerManager::UiNavigationState::* member) {
                    return (current.*member) && !(state->lastControllerUiState.*member);
                };
                if (!state->captureArmed) {
                    const HWND focus = GetFocus();
                    if (pressed(&ControllerManager::UiNavigationState::up)) {
                        if (focus == state->captureButton || GetDlgItem(state->hwnd, IDC_EDIT_STEP) == focus ||
                            GetDlgItem(state->hwnd, IDC_DELETE_STEP) == focus) {
                            FocusRecorderControl(state, state->list);
                        } else if (GetDlgItem(state->hwnd, IDC_CLEAR_ALL) == focus) {
                            FocusRecorderControl(state, state->captureButton);
                        } else if (state->saveButton == focus) {
                            FocusRecorderControl(state, GetDlgItem(state->hwnd, IDC_EDIT_STEP));
                        } else if (state->cancelButton == focus) {
                            FocusRecorderControl(state, GetDlgItem(state->hwnd, IDC_DELETE_STEP));
                        } else {
                            MoveRecorderFocus(state, -1);
                        }
                    }
                    if (pressed(&ControllerManager::UiNavigationState::down)) {
                        if (focus == state->list) {
                            FocusRecorderControl(state, state->captureButton);
                        } else if (focus == state->captureButton) {
                            FocusRecorderControl(state, GetDlgItem(state->hwnd, IDC_CLEAR_ALL));
                        } else if (GetDlgItem(state->hwnd, IDC_EDIT_STEP) == focus) {
                            FocusRecorderControl(state, state->saveButton);
                        } else if (GetDlgItem(state->hwnd, IDC_DELETE_STEP) == focus) {
                            FocusRecorderControl(state, state->cancelButton);
                        } else {
                            MoveRecorderFocus(state, 1);
                        }
                    }
                    if (pressed(&ControllerManager::UiNavigationState::left)) {
                        if (focus == state->list)
                            SelectListDelta(state, -1);
                        else
                            MoveRecorderFocus(state, -1);
                    }
                    if (pressed(&ControllerManager::UiNavigationState::right)) {
                        if (focus == state->list)
                            SelectListDelta(state, 1);
                        else
                            MoveRecorderFocus(state, 1);
                    }
                    if (pressed(&ControllerManager::UiNavigationState::confirm)) {
                        if (focus == state->list)
                            BeginEditSelectedStep(state);
                        else if (focus)
                            SendMessageW(focus, BM_CLICK, 0, 0);
                    }
                }
                state->lastControllerUiState = current;
            }

            if (state->controllerChordFn) {
                const std::wstring chord = state->controllerChordFn();
                if (state->captureArmed && state->waitingForChordRelease) {
                    if (chord.empty()) {
                        state->waitingForChordRelease = false;
                        SetStatus(state, state->appendIndex >= 0
                            ? L"Listening for append chord..."
                            : L"Listening for new step chord...");
                    }
                } else if (!chord.empty() && state->lastControllerChord.empty()) {
                    if (IsControllerSaveChord(chord)) {
                        FinishRecorder(state, true);
                        return 0;
                    }
                    if (IsControllerCancelChord(chord)) {
                        FinishRecorder(state, false);
                        return 0;
                    }
                    if (state->captureArmed) {
                        AddOrMergeStep(state, chord);
                        state->captureArmed = false;
                        state->captureToSelected = false;
                        state->waitingForChordRelease = false;
                        state->appendIndex = -1;
                        RenderList(state);
                        SetStatus(state, L"Captured: " + chord + L". Use Capture Step for another.");
                        RefreshRecorderFocus(state);
                    }
                }
                state->lastControllerChord = chord;
            }
        }
        return 0;
    case WM_CLOSE:
        state->accepted = false;
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, TIMER_CONTROLLER_POLL);
        g_recorderWindow = nullptr;
        return 0;
    }

    return DefWindowProcW(hwnd, msg, wp, lp);
}
}

bool MacroRecorder::Record(HWND owner, std::wstring& macroText, const std::wstring& initialMacroText,
                           ControllerChordFn controllerChordFn,
                           ControllerUiStateFn controllerUiStateFn) {
    if (g_recorderWindow && IsWindow(g_recorderWindow)) {
        ShowWindow(g_recorderWindow, SW_SHOWNORMAL);
        SetForegroundWindow(g_recorderWindow);
        return false;
    }

    WNDCLASSW wc{};
    wc.lpfnWndProc = RecorderProc;
    wc.hInstance = reinterpret_cast<HINSTANCE>(GetWindowLongPtrW(owner, GWLP_HINSTANCE));
    wc.lpszClassName = kClassName;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    RegisterClassW(&wc);

    RecorderState state{};
    state.controllerChordFn = std::move(controllerChordFn);
    state.controllerUiStateFn = std::move(controllerUiStateFn);
    state.focusIndex = 1;
    if (state.controllerUiStateFn)
        state.lastControllerUiState = state.controllerUiStateFn();
    if (state.controllerChordFn)
        state.lastControllerChord = state.controllerChordFn();
    {
        std::wstringstream stream(initialMacroText);
        std::wstring step;
        while (std::getline(stream, step, L',')) {
            step = Trim(step);
            if (!step.empty())
                state.steps.push_back(step);
        }
    }
    HWND hwnd = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_TOPMOST,
        kClassName,
        L"Capture Input",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        CW_USEDEFAULT, CW_USEDEFAULT, 486, 430,
        owner, nullptr, wc.hInstance, &state);
    if (!hwnd)
        return false;

    if (owner && IsWindow(owner))
        EnableWindow(owner, FALSE);
    g_recorderWindow = hwnd;
    ShowWindow(hwnd, SW_SHOWNORMAL);
    UpdateWindow(hwnd);
    SetForegroundWindow(hwnd);
    SetActiveWindow(hwnd);
    RefreshRecorderFocus(&state);
    g_activeRecorder = &state;
    g_keyboardHook = SetWindowsHookExW(WH_KEYBOARD_LL, KeyboardHookProc, GetModuleHandleW(nullptr), 0);

    MSG msg;
    while (IsWindow(hwnd) && GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(hwnd, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
    }

    if (g_keyboardHook) {
        UnhookWindowsHookEx(g_keyboardHook);
        g_keyboardHook = nullptr;
    }
    g_activeRecorder = nullptr;
    if (owner && IsWindow(owner)) {
        EnableWindow(owner, TRUE);
        SetActiveWindow(owner);
        SetForegroundWindow(owner);
    }

    if (!state.accepted)
        return false;

    macroText = state.result;
    return true;
}
