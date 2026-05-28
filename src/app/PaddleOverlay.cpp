#include "PaddleOverlay.h"
#include "logging/Log.h"
#include "steam/SteamController.h"
#include <Windows.h>
#include <array>
#include <thread>
#include <utility>
#include <vector>

namespace {
bool IsPressed(const uint8_t* buf, size_t n, int paddleIndex, const StandardGamepadState* standardState) {
    if (standardState && standardState->connected) {
        switch (paddleIndex) {
        case 0: return standardState->leftPaddle1;
        case 1: return standardState->leftPaddle2;
        case 2: return standardState->rightPaddle1;
        case 3: return standardState->rightPaddle2;
        case 4: return standardState->misc1 || standardState->touchpadButton;
        default: return false;
        }
    }

    if (!SteamController::UsesLegacyStateLayout(buf, n))
        return false;

    const uint8_t b0 = buf[2];
    const uint8_t b1 = buf[3];
    const uint8_t b2 = buf[4];

    switch (paddleIndex) {
    case 0: return (b2 & SteamController::BTN_L4) != 0;
    case 1: return (b2 & SteamController::BTN_L5) != 0;
    case 2: return (b0 & SteamController::BTN_R4) != 0;
    case 3: return (b1 & SteamController::BTN_R5) != 0;
    case 4: return (b0 & SteamController::BTN_QAM) != 0;
    default: return false;
    }
}

const PaddleAction& GetAction(const PaddleActionBindings& bindings, int paddleIndex) {
    switch (paddleIndex) {
    case 0: return bindings.l4;
    case 1: return bindings.l5;
    case 2: return bindings.r4;
    case 3: return bindings.r5;
    default: return bindings.qam;
    }
}

const wchar_t* PaddleName(int paddleIndex) {
    switch (paddleIndex) {
    case 0: return L"L4";
    case 1: return L"L5";
    case 2: return L"R4";
    case 3: return L"R5";
    default: return L"QAM";
    }
}

const char* ActionTypeName(PaddleActionType type) {
    switch (type) {
    case PaddleActionType::UseMenuMapping: return "menu";
    case PaddleActionType::None: return "none";
    case PaddleActionType::Gamepad: return "gamepad";
    case PaddleActionType::KeyChord: return "key";
    case PaddleActionType::Macro: return "macro";
    }
    return "unknown";
}

void RunMacro(std::vector<std::vector<uint16_t>> steps, PaddleOverlay::KeyChordCallback cb) {
    std::thread([steps = std::move(steps), cb = std::move(cb)]() {
        for (const auto& chord : steps) {
            if (cb) cb(chord, true);
            Sleep(20);
            if (cb) cb(chord, false);
            Sleep(30);
        }
    }).detach();
}

void TapChord(const std::vector<uint16_t>& chord, const PaddleOverlay::KeyChordCallback& cb) {
    if (cb) cb(chord, true);
    Sleep(15);
    if (cb) cb(chord, false);
}
}

void PaddleOverlay::SetBindings(PaddleActionBindings bindings) {
    m_bindings = std::move(bindings);
}

void PaddleOverlay::Reset() {
    for (int i = 0; i < 5; ++i) {
        const PaddleAction& action = GetAction(m_bindings, i);
        if (m_prevPressed[i] &&
            action.type == PaddleActionType::KeyChord &&
            !action.rapidFire &&
            m_keyChordCallback) {
            m_keyChordCallback(action.chord, false);
        }
        m_prevPressed[i] = false;
        m_lastFireTickMs[i] = 0;
    }
    m_hasSeededState = false;
}

void PaddleOverlay::Update(const uint8_t* buf, size_t n, const StandardGamepadState* standardState) {
    if ((!standardState || !standardState->connected) && !SteamController::UsesLegacyStateLayout(buf, n))
        return;

    if (!m_hasSeededState) {
        for (int i = 0; i < 5; ++i)
            m_prevPressed[i] = IsPressed(buf, n, i, standardState);
        m_hasSeededState = true;
        logging::Logf("[PaddleOverlay] Seeded initial paddle state L4=%d L5=%d R4=%d R5=%d QAM=%d",
                      m_prevPressed[0] ? 1 : 0,
                      m_prevPressed[1] ? 1 : 0,
                      m_prevPressed[2] ? 1 : 0,
                      m_prevPressed[3] ? 1 : 0,
                      m_prevPressed[4] ? 1 : 0);
        return;
    }

    for (int i = 0; i < 5; ++i) {
        const PaddleAction& action = GetAction(m_bindings, i);
        const bool pressed = IsPressed(buf, n, i, standardState);
        const ULONGLONG now = GetTickCount64();

        if (pressed) {
            const bool rising = !m_prevPressed[i];
            const bool rapidReady = action.rapidFire &&
                (rising || (now - m_lastFireTickMs[i]) >= 90);

            if (action.type == PaddleActionType::KeyChord) {
                if (action.rapidFire && rapidReady) {
                    logging::Logf("[PaddleOverlay] Fire paddle=%S action=%s rapid=1", PaddleName(i), ActionTypeName(action.type));
                    TapChord(action.chord, m_keyChordCallback);
                    m_lastFireTickMs[i] = now;
                } else if (rising) {
                    logging::Logf("[PaddleOverlay] Down paddle=%S action=%s rapid=0", PaddleName(i), ActionTypeName(action.type));
                    if (m_keyChordCallback) m_keyChordCallback(action.chord, true);
                }
            } else if (action.type == PaddleActionType::Macro) {
                if (rising || rapidReady) {
                    logging::Logf("[PaddleOverlay] Fire paddle=%S action=%s rapid=%d", PaddleName(i), ActionTypeName(action.type), action.rapidFire ? 1 : 0);
                    RunMacro(action.macroSteps, m_keyChordCallback);
                    m_lastFireTickMs[i] = now;
                }
            }
        } else if (!pressed && m_prevPressed[i]) {
            if (action.type == PaddleActionType::KeyChord && !action.rapidFire) {
                logging::Logf("[PaddleOverlay] Up paddle=%S action=%s", PaddleName(i), ActionTypeName(action.type));
                if (m_keyChordCallback) m_keyChordCallback(action.chord, false);
            }
        }

        m_prevPressed[i] = pressed;
    }
}
