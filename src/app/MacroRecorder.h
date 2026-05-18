#pragma once
#include "ControllerManager.h"
#include <Windows.h>
#include <functional>
#include <string>

class MacroRecorder {
public:
    using ControllerChordFn = std::function<std::wstring()>;
    using ControllerUiState = ControllerManager::UiNavigationState;
    using ControllerUiStateFn = std::function<ControllerUiState()>;
    static bool Record(HWND owner, std::wstring& macroText, const std::wstring& initialMacroText = L"",
                       ControllerChordFn controllerChordFn = {},
                       ControllerUiStateFn controllerUiStateFn = {});
};
