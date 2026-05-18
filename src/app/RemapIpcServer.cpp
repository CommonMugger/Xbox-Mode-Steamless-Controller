#include "RemapIpcServer.h"
#include "logging/Log.h"
#include <AclAPI.h>
#include <sddl.h>
#include <vector>

namespace {
std::string ReadPipeRequest(HANDLE pipe) {
    std::string request;
    char buffer[512];
    DWORD bytesRead = 0;
    while (ReadFile(pipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
        request.append(buffer, buffer + bytesRead);
        if (request.find('\n') != std::string::npos)
            break;
    }

    const size_t newline = request.find_first_of("\r\n");
    if (newline != std::string::npos)
        request.erase(newline);
    return request;
}

void WritePipeResponse(HANDLE pipe, const std::string& response) {
    std::string payload = response;
    payload.push_back('\n');
    DWORD bytesWritten = 0;
    WriteFile(pipe, payload.data(), static_cast<DWORD>(payload.size()), &bytesWritten, nullptr);
    FlushFileBuffers(pipe);
}

SECURITY_ATTRIBUTES BuildPipeSecurityAttributes(PSECURITY_DESCRIPTOR& securityDescriptor) {
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof(attributes);

    // Allow the current desktop app, standard desktop clients, and packaged/UWP clients.
    if (!ConvertStringSecurityDescriptorToSecurityDescriptorW(
            L"D:(A;;GA;;;SY)(A;;GA;;;BA)(A;;GRGW;;;WD)(A;;GRGW;;;AC)",
            SDDL_REVISION_1,
            &securityDescriptor,
            nullptr)) {
        securityDescriptor = nullptr;
        return attributes;
    }

    attributes.lpSecurityDescriptor = securityDescriptor;
    attributes.bInheritHandle = FALSE;
    return attributes;
}
}

RemapIpcServer::RemapIpcServer(RequestHandlerFn handler)
    : m_handler(std::move(handler)) {}

RemapIpcServer::~RemapIpcServer() {
    Stop();
}

std::wstring RemapIpcServer::PipeName() {
    return L"\\\\.\\pipe\\SteamControllerRemapperControl";
}

void RemapIpcServer::Start() {
    if (m_running.exchange(true))
        return;

    m_stopEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    m_thread = std::thread(&RemapIpcServer::Run, this);
}

void RemapIpcServer::Stop() {
    if (!m_running.exchange(false))
        return;

    if (m_stopEvent)
        SetEvent(m_stopEvent);

    {
        HANDLE pipe = CreateFileW(PipeName().c_str(), GENERIC_READ | GENERIC_WRITE, 0, nullptr, OPEN_EXISTING, 0, nullptr);
        if (pipe != INVALID_HANDLE_VALUE) {
            CloseHandle(pipe);
        }
    }

    if (m_thread.joinable())
        m_thread.join();

    if (m_stopEvent) {
        CloseHandle(m_stopEvent);
        m_stopEvent = nullptr;
    }
}

void RemapIpcServer::Run() {
    while (m_running) {
        PSECURITY_DESCRIPTOR securityDescriptor = nullptr;
        SECURITY_ATTRIBUTES securityAttributes = BuildPipeSecurityAttributes(securityDescriptor);
        HANDLE pipe = CreateNamedPipeW(
            PipeName().c_str(),
            PIPE_ACCESS_DUPLEX,
            PIPE_TYPE_BYTE | PIPE_READMODE_BYTE | PIPE_WAIT,
            1,
            4096,
            4096,
            0,
            securityDescriptor ? &securityAttributes : nullptr);

        if (securityDescriptor)
            LocalFree(securityDescriptor);

        if (pipe == INVALID_HANDLE_VALUE) {
            logging::Logf("[IPC] CreateNamedPipe failed");
            return;
        }

        OVERLAPPED overlapped{};
        overlapped.hEvent = CreateEventW(nullptr, TRUE, FALSE, nullptr);
        const BOOL connectStarted = ConnectNamedPipe(pipe, &overlapped);
        const DWORD connectError = connectStarted ? ERROR_SUCCESS : GetLastError();

        HANDLE waitHandles[] = { overlapped.hEvent, m_stopEvent };
        DWORD waitResult = WAIT_FAILED;

        if (!connectStarted && connectError == ERROR_PIPE_CONNECTED) {
            SetEvent(overlapped.hEvent);
        }

        if (connectStarted || connectError == ERROR_IO_PENDING || connectError == ERROR_PIPE_CONNECTED) {
            waitResult = WaitForMultipleObjects(2, waitHandles, FALSE, INFINITE);
        }

        CloseHandle(overlapped.hEvent);

        if (!m_running || waitResult == WAIT_OBJECT_0 + 1) {
            CloseHandle(pipe);
            break;
        }

        if (waitResult == WAIT_OBJECT_0) {
            const std::string request = ReadPipeRequest(pipe);
            const std::string response = m_handler ? m_handler(request) : std::string("ERR\tno-handler");
            WritePipeResponse(pipe, response);
        }

        DisconnectNamedPipe(pipe);
        CloseHandle(pipe);
    }
}
