#pragma once

#include <Windows.h>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <direct.h>
#include <mutex>
#include <string>

namespace logging {

inline std::wstring LogDirectory() {
    wchar_t path[MAX_PATH];
    DWORD n = GetEnvironmentVariableW(L"LOCALAPPDATA", path, MAX_PATH);
    if (n == 0 || n >= MAX_PATH)
        return L".";
    return std::wstring(path) + L"\\XboxModeSteamlessController";
}

inline std::wstring LogPath() {
    return LogDirectory() + L"\\steamless.log";
}

inline void EnsureLogDirectory() {
    std::wstring dir = LogDirectory();
    _wmkdir(dir.c_str());
}

inline void WriteLine(const char* line) {
    static std::mutex mutex;
    static bool wroteHeader = false;
    std::lock_guard<std::mutex> lock(mutex);

    EnsureLogDirectory();
    FILE* file = nullptr;
    if (_wfopen_s(&file, LogPath().c_str(), L"ab") != 0 || !file)
        return;

    if (!wroteHeader) {
        const unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        long pos = std::ftell(file);
        if (pos == 0)
            std::fwrite(bom, 1, sizeof(bom), file);
        wroteHeader = true;
    }

    SYSTEMTIME st{};
    GetLocalTime(&st);
    std::fprintf(file, "%04u-%02u-%02u %02u:%02u:%02u.%03u %s\n",
                 st.wYear, st.wMonth, st.wDay,
                 st.wHour, st.wMinute, st.wSecond, st.wMilliseconds,
                 line);
    std::fclose(file);
}

inline void Logf(const char* fmt, ...) {
    char buffer[1024];
    va_list args;
    va_start(args, fmt);
    std::vsnprintf(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    buffer[sizeof(buffer) - 1] = '\0';
    WriteLine(buffer);
}

inline std::string Narrow(const std::wstring& value) {
    if (value.empty())
        return {};

    int needed = WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, nullptr, 0, nullptr, nullptr);
    if (needed <= 1)
        return {};

    std::string out(static_cast<size_t>(needed - 1), '\0');
    WideCharToMultiByte(CP_UTF8, 0, value.c_str(), -1, out.data(), needed, nullptr, nullptr);
    return out;
}

} // namespace logging
