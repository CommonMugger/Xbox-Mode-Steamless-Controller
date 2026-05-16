#include "TrayApp.h"
#include <Windows.h>

int WINAPI wWinMain(HINSTANCE hInstance, HINSTANCE, PWSTR, int) {
    // Prevent multiple instances.
    HANDLE mutex = CreateMutexW(nullptr, TRUE, L"XboxModeSteamlessController_SingleInstance");
    if (!mutex || GetLastError() == ERROR_ALREADY_EXISTS) {
        if (mutex) CloseHandle(mutex);
        return 0;
    }

    TrayApp app;
    int result = 0;
    if (app.Init(hInstance))
        result = app.Run();

    CloseHandle(mutex);
    return result;
}
