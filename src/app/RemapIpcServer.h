#pragma once

#include <Windows.h>
#include <atomic>
#include <functional>
#include <string>
#include <thread>

class RemapIpcServer {
public:
    using RequestHandlerFn = std::function<std::string(const std::string&)>;

    explicit RemapIpcServer(RequestHandlerFn handler);
    ~RemapIpcServer();

    void Start();
    void Stop();

private:
    void Run();
    static std::wstring PipeName();

    RequestHandlerFn m_handler;
    std::atomic<bool> m_running{ false };
    HANDLE m_stopEvent = nullptr;
    std::thread m_thread;
};
