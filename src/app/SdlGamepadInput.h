#pragma once
#include "StandardGamepadState.h"
#include <cstdint>

class SdlGamepadInput {
public:
    SdlGamepadInput();
    ~SdlGamepadInput();
    SdlGamepadInput(const SdlGamepadInput&) = delete;
    SdlGamepadInput& operator=(const SdlGamepadInput&) = delete;

    bool Poll(StandardGamepadState& state);

private:
    bool EnsureOpen();
    bool OpenMatchingGamepad();
    void CloseCurrent();

    void* m_gamepad = nullptr;
    bool m_backendReady = false;
    std::uint64_t m_lastOpenAttemptTickMs = 0;
    bool m_loggedUnavailable = false;
};
