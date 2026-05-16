#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <setupapi.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>
#include "HidDevice.h"
#include "logging/Log.h"

// ---------------------------------------------------------------------------
// Enumeration
// ---------------------------------------------------------------------------

std::vector<std::wstring> HidDevice::Enumerate(uint16_t vid, uint16_t pid, uint16_t usagePage) {
    GUID hidGuid;
    HidD_GetHidGuid(&hidGuid);

    HDEVINFO devInfo = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr,
                                             DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE)
        return {};

    std::vector<std::wstring> paths;
    SP_DEVICE_INTERFACE_DATA ifaceData{};
    ifaceData.cbSize = sizeof(ifaceData);

    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &hidGuid, i, &ifaceData); ++i) {
        DWORD needed = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, nullptr, 0, &needed, nullptr);
        if (needed == 0)
            continue;

        auto detailBuf = std::make_unique<uint8_t[]>(needed);
        auto* detail   = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W*>(detailBuf.get());
        detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);

        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifaceData, detail, needed, nullptr, nullptr))
            continue;

        // Open with no access just to query attributes — avoids needing exclusive open.
        HANDLE h = CreateFileW(detail->DevicePath, 0,
                               FILE_SHARE_READ | FILE_SHARE_WRITE,
                               nullptr, OPEN_EXISTING, 0, nullptr);
        if (h == INVALID_HANDLE_VALUE)
            continue;

        HIDD_ATTRIBUTES attrs{};
        attrs.Size = sizeof(attrs);
        bool match = HidD_GetAttributes(h, &attrs)
                  && attrs.VendorID == vid
                  && (pid == 0 || attrs.ProductID == pid);

        if (match && usagePage != 0) {
            PHIDP_PREPARSED_DATA preparsed;
            if (HidD_GetPreparsedData(h, &preparsed)) {
                HIDP_CAPS caps{};
                if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS)
                    match = (caps.UsagePage == usagePage);
                HidD_FreePreparsedData(preparsed);
            } else {
                match = false;
            }
        }

        if (match)
            paths.push_back(detail->DevicePath);

        CloseHandle(h);
    }

    SetupDiDestroyDeviceInfoList(devInfo);
    return paths;
}

// ---------------------------------------------------------------------------
// Move semantics
// ---------------------------------------------------------------------------

HidDevice::HidDevice(HidDevice&& o) noexcept
    : m_handle(o.m_handle), m_event(o.m_event), m_outputReportLen(o.m_outputReportLen) {
    o.m_handle = INVALID_HANDLE_VALUE;
    o.m_event  = INVALID_HANDLE_VALUE;
}

HidDevice& HidDevice::operator=(HidDevice&& o) noexcept {
    if (this != &o) {
        Close();
        m_handle          = o.m_handle;
        m_event           = o.m_event;
        m_outputReportLen = o.m_outputReportLen;
        o.m_handle = INVALID_HANDLE_VALUE;
        o.m_event  = INVALID_HANDLE_VALUE;
    }
    return *this;
}

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

bool HidDevice::Open(const std::wstring& path) {
    Close();
    logging::Logf("[HidDevice] Open path=%s", logging::Narrow(path).c_str());

    // FILE_SHARE_READ | FILE_SHARE_WRITE lets us coexist with Steam's open handle.
    // Remove the share flags for exclusive access (Steam must be closed first).
    m_handle = CreateFileW(path.c_str(),
                           GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE,
                           nullptr,
                           OPEN_EXISTING,
                           FILE_FLAG_OVERLAPPED,
                           nullptr);

    if (m_handle == INVALID_HANDLE_VALUE) {
        wprintf(L"CreateFileW failed for %s: error %lu\n", path.c_str(), GetLastError());
        logging::Logf("[HidDevice] Open failed error=%lu", GetLastError());
        return false;
    }

    m_event = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    if (m_event == INVALID_HANDLE_VALUE) {
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
        logging::Logf("[HidDevice] CreateEvent failed error=%lu", GetLastError());
        return false;
    }

    // Query report sizes. HidD_SetOutputReport and HidD_SetFeature each require
    // their buffer to be exactly the maximum report length of their respective type.
    PHIDP_PREPARSED_DATA preparsed;
    if (HidD_GetPreparsedData(m_handle, &preparsed)) {
        HIDP_CAPS caps{};
        if (HidP_GetCaps(preparsed, &caps) == HIDP_STATUS_SUCCESS) {
            if (caps.OutputReportByteLength  > 0) m_outputReportLen  = caps.OutputReportByteLength;
            if (caps.FeatureReportByteLength > 0) m_featureReportLen = caps.FeatureReportByteLength;
        }
        HidD_FreePreparsedData(preparsed);
    }
    logging::Logf("[HidDevice] Open success outputLen=%lu featureLen=%lu",
                  m_outputReportLen, m_featureReportLen);
    return true;
}

void HidDevice::Close() {
    if (m_handle != INVALID_HANDLE_VALUE) {
        CancelIo(m_handle);
        CloseHandle(m_handle);
        m_handle = INVALID_HANDLE_VALUE;
    }
    if (m_event != INVALID_HANDLE_VALUE) {
        CloseHandle(m_event);
        m_event = INVALID_HANDLE_VALUE;
    }
}

// ---------------------------------------------------------------------------
// I/O
// ---------------------------------------------------------------------------

bool HidDevice::SendOutputReport(const uint8_t* data, size_t size) {
    // HidD_SetOutputReport requires the buffer to be exactly OutputReportByteLength.
    // Pad with zeros if the caller provided a shorter buffer (e.g. a 7-byte report
    // when the device's max output report is 64 bytes).
    if (size < m_outputReportLen) {
        std::vector<uint8_t> padded(m_outputReportLen, 0);
        std::memcpy(padded.data(), data, size);
        BOOLEAN ok = HidD_SetOutputReport(m_handle, padded.data(), m_outputReportLen);
        if (!ok)
            printf("SendOutputReport(0x%02X) failed: error %lu\n", data[0], GetLastError());
        return ok == TRUE;
    }

    BOOLEAN ok = HidD_SetOutputReport(m_handle,
                                       const_cast<PVOID>(static_cast<const void*>(data)),
                                       static_cast<ULONG>(size));
    if (!ok)
        printf("SendOutputReport(0x%02X) failed: error %lu\n", data[0], GetLastError());
    return ok == TRUE;
}

bool HidDevice::SendFeatureReport(const uint8_t* data, size_t size) {
    std::vector<uint8_t> buf(m_featureReportLen, 0);
    size_t copyLen = size < m_featureReportLen ? size : m_featureReportLen;
    std::memcpy(buf.data(), data, copyLen);
    BOOLEAN ok = HidD_SetFeature(m_handle, buf.data(), m_featureReportLen);
    if (!ok)
        printf("SendFeatureReport(reportId=0x%02X cmd=0x%02X) failed: error %lu\n",
               buf[0], buf[1], GetLastError());
    if (!ok) {
        logging::Logf("[HidDevice] SendFeatureReport failed reportId=0x%02X cmd=0x%02X error=%lu featureLen=%lu",
                      buf[0], buf[1], GetLastError(), m_featureReportLen);
    }
    return ok == TRUE;
}

bool HidDevice::WriteOutputPacket(const uint8_t* data, size_t size, uint32_t timeoutMs) {
    OVERLAPPED ov{};
    ov.hEvent = m_event;
    ResetEvent(m_event);

    DWORD bytesWritten = 0;
    if (!WriteFile(m_handle, data, static_cast<DWORD>(size), &bytesWritten, &ov)) {
        DWORD err = GetLastError();
        if (err != ERROR_IO_PENDING) {
            logging::Logf("[HidDevice] WriteOutputPacket failed reportId=0x%02X error=%lu",
                          size > 0 ? data[0] : 0, err);
            return false;
        }

        DWORD wait = WaitForSingleObject(m_event, timeoutMs);
        if (wait != WAIT_OBJECT_0) {
            CancelIo(m_handle);
            logging::Logf("[HidDevice] WriteOutputPacket timeout reportId=0x%02X wait=%lu",
                          size > 0 ? data[0] : 0, wait);
            return false;
        }
        if (!GetOverlappedResult(m_handle, &ov, &bytesWritten, FALSE)) {
            logging::Logf("[HidDevice] WriteOutputPacket overlapped result failed reportId=0x%02X error=%lu",
                          size > 0 ? data[0] : 0, GetLastError());
            return false;
        }
    }

    const bool ok = bytesWritten == size;
    logging::Logf("[HidDevice] WriteOutputPacket reportId=0x%02X size=%zu bytesWritten=%lu ok=%d",
                  size > 0 ? data[0] : 0,
                  size,
                  bytesWritten,
                  ok ? 1 : 0);
    return ok;
}

size_t HidDevice::ReadInputReport(uint8_t* buffer, size_t size, uint32_t timeoutMs) {
    OVERLAPPED ov{};
    ov.hEvent = m_event;
    ResetEvent(m_event);

    DWORD bytesRead = 0;
    if (!ReadFile(m_handle, buffer, static_cast<DWORD>(size), &bytesRead, &ov)) {
        if (GetLastError() != ERROR_IO_PENDING)
            return 0;

        DWORD wait = WaitForSingleObject(m_event, timeoutMs);
        if (wait != WAIT_OBJECT_0) {
            CancelIo(m_handle);
            return 0;
        }
        if (!GetOverlappedResult(m_handle, &ov, &bytesRead, FALSE))
            return 0;
    }

    return static_cast<size_t>(bytesRead);
}
