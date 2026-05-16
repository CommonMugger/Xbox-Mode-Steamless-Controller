#include "SteamController.h"
#include "logging/Log.h"
#include <chrono>
#include <cstdio>
#include <cstring>

// ---------------------------------------------------------------------------
// Internal helper: build a 64-byte feature report command buffer.
//
// The 2026 Steam Controller routes firmware commands through Feature Report
// 0x01 (same channel the original SC used, now with an explicit report ID).
//
// Buffer layout:
//   [0] FEATURE_REPORT_CMD (0x01)  — HID feature report ID
//   [1] cmd                         — command byte (0x81, 0x87, etc.)
//   [2] payloadSize                 — number of payload bytes that follow
//   [3..3+payloadSize-1] payload    — command arguments
//   [rest] zeros
// ---------------------------------------------------------------------------

static void BuildCmd(uint8_t (&buf)[64], uint8_t cmd,
                     const uint8_t* payload = nullptr, uint8_t payloadSize = 0) {
    std::memset(buf, 0, 64);
    buf[0] = SteamController::FEATURE_REPORT_CMD;
    buf[1] = cmd;
    buf[2] = payloadSize;
    if (payload && payloadSize)
        std::memcpy(buf + 3, payload, payloadSize);
}

// ---------------------------------------------------------------------------
// Open / Close
// ---------------------------------------------------------------------------

bool SteamController::Open() {
    logging::Logf("[SteamController] Open begin");
    for (uint16_t pid : { SC2026_PID, SC2026_DONGLE_PID }) {
        auto paths = HidDevice::Enumerate(VALVE_VID, pid, VENDOR_USAGE_PAGE);
        logging::Logf("[SteamController] Enumerate pid=%04X paths=%zu", pid, paths.size());
        if (paths.empty()) continue;

        // For the wired controller there is only one interface; for the dongle
        // there are up to four slots (one per paired controller). Try each in
        // order and use the first that produces a live input report.
        for (auto const& path : paths) {
            logging::Logf("[SteamController] Trying path pid=%04X path=%s",
                          pid, logging::Narrow(path).c_str());
            if (!m_device.Open(path)) continue;

            uint8_t buf[64];
            size_t n = m_device.ReadInputReport(buf, sizeof(buf), /*timeoutMs=*/500);
            if (n > 0 && buf[0] == REPORT_STATE) {
                printf("Active interface found for PID=%04X.\n", pid);
                logging::Logf("[SteamController] Active interface pid=%04X reportBytes=%zu reportId=0x%02X",
                              pid, n, buf[0]);
                return true;
            }

            logging::Logf("[SteamController] Path rejected pid=%04X reportBytes=%zu reportId=0x%02X",
                          pid, n, n > 0 ? buf[0] : 0);

            m_device.Close();
        }
    }

    printf("No Steam Controller found (wired PID=%04X or dongle PID=%04X).\n",
           SC2026_PID, SC2026_DONGLE_PID);
    logging::Logf("[SteamController] Open failed");
    return false;
}

static uint16_t ScaleMotorToAmplitude(uint8_t motor) {
    // Map XInput's 0-255 motor range into the 0-0x7FFF range used by the
    // Steam Controller haptic packet. Zero stays silent.
    return static_cast<uint16_t>((static_cast<uint32_t>(motor) * 0x7FFFu) / 0xFFu);
}

void SteamController::Close() {
    logging::Logf("[SteamController] Close");
    if (m_running.exchange(false) && m_heartbeat.joinable())
        m_heartbeat.join();
    {
        std::lock_guard<std::mutex> lock(m_rumbleMutex);
        m_rumbleStop = true;
        m_largeMotor = 0;
        m_smallMotor = 0;
    }
    m_rumbleCv.notify_all();
    if (m_rumbleThread.joinable())
        m_rumbleThread.join();
    m_device.Close();
}

// ---------------------------------------------------------------------------
// Lizard mode
// ---------------------------------------------------------------------------

bool SteamController::DisableLizardMode() {
    uint8_t buf[64];
    logging::Logf("[SteamController] DisableLizardMode begin");

    // Step 1: CLEAR_DIGITAL_MAPPINGS — kills keyboard/mouse button emulation.
    BuildCmd(buf, CMD_CLEAR_DIGITAL_MAPPINGS);
    {
        std::lock_guard<std::mutex> lock(m_featureMutex);
        if (!m_device.SendFeatureReport(buf, sizeof(buf))) {
            printf("Failed to send CLEAR_DIGITAL_MAPPINGS.\n");
            logging::Logf("[SteamController] DisableLizardMode failed step=CLEAR_DIGITAL_MAPPINGS");
            return false;
        }
    }

    // Step 2: SET_SETTINGS — set both trackpads to TRACKPAD_NONE.
    // Payload: pairs of [setting_id, val_lo, val_hi].
    const uint8_t settingsPayload[] = {
        SETTING_LEFT_TRACKPAD_MODE,  0x00, 0x00,
        SETTING_RIGHT_TRACKPAD_MODE, 0x00, 0x00,
    };
    BuildCmd(buf, CMD_SET_SETTINGS, settingsPayload, sizeof(settingsPayload));
    {
        std::lock_guard<std::mutex> lock(m_featureMutex);
        if (!m_device.SendFeatureReport(buf, sizeof(buf))) {
            printf("Failed to send SET_SETTINGS_VALUES.\n");
            logging::Logf("[SteamController] DisableLizardMode failed step=SET_SETTINGS");
            return false;
        }
    }

    if (!m_running.exchange(true))
        m_heartbeat = std::thread(&SteamController::HeartbeatLoop, this);

    {
        std::lock_guard<std::mutex> lock(m_rumbleMutex);
        m_rumbleStop = false;
    }

    logging::Logf("[SteamController] DisableLizardMode success");
    return true;
}

bool SteamController::EnableLizardMode() {
    logging::Logf("[SteamController] EnableLizardMode begin");
    if (m_running.exchange(false) && m_heartbeat.joinable())
        m_heartbeat.join();

    uint8_t buf[64];
    BuildCmd(buf, CMD_SET_DEFAULT_MAPPINGS);
    bool ok = false;
    {
        std::lock_guard<std::mutex> lock(m_featureMutex);
        ok = m_device.SendFeatureReport(buf, sizeof(buf));
    }
    logging::Logf("[SteamController] EnableLizardMode %s", ok ? "success" : "failed");
    return ok;
}

// ---------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------

size_t SteamController::ReadReport(uint8_t* buffer, size_t size, uint32_t timeoutMs) {
    return m_device.ReadInputReport(buffer, size, timeoutMs);
}

void SteamController::SetRumble(uint8_t largeMotor, uint8_t smallMotor) {
    {
        std::lock_guard<std::mutex> lock(m_rumbleMutex);
        m_largeMotor = largeMotor;
        m_smallMotor = smallMotor;
        if (!m_rumbleThread.joinable()) {
            m_rumbleStop = false;
            m_rumbleThread = std::thread(&SteamController::RumbleLoop, this);
        }
    }

    if (largeMotor != 0 || smallMotor != 0) {
        logging::Logf("[SteamController] SetRumble large=%u small=%u",
                      static_cast<unsigned>(largeMotor),
                      static_cast<unsigned>(smallMotor));
    }
    m_rumbleCv.notify_one();
}


// ---------------------------------------------------------------------------
// Heartbeat
// ---------------------------------------------------------------------------

void SteamController::HeartbeatLoop() {
    uint8_t buf[64];
    BuildCmd(buf, CMD_CLEAR_DIGITAL_MAPPINGS);
    logging::Logf("[SteamController] Heartbeat start");

    while (m_running.load()) {
        {
            std::lock_guard<std::mutex> lock(m_featureMutex);
            m_device.SendFeatureReport(buf, sizeof(buf));
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(800));
    }

    logging::Logf("[SteamController] Heartbeat stop");
}

bool SteamController::SendHapticCommand(uint8_t position, uint16_t amplitude, uint16_t period, uint16_t count) {
    (void)position;
    (void)amplitude;
    (void)period;
    (void)count;

    // Captured from Steam's own haptics test: a raw 4-byte interrupt-OUT packet.
    // This appears to be the actual actuator trigger path, unlike the 0x8F
    // feature command we previously tried to synthesize.
    const uint8_t outPacket[] = { 0x82, 0x00, 0x02, 0x00 };

    uint8_t payload[7];
    payload[0] = position;
    payload[1] = static_cast<uint8_t>(amplitude & 0xFF);
    payload[2] = static_cast<uint8_t>((amplitude >> 8) & 0xFF);
    payload[3] = static_cast<uint8_t>(period & 0xFF);
    payload[4] = static_cast<uint8_t>((period >> 8) & 0xFF);
    payload[5] = static_cast<uint8_t>(count & 0xFF);
    payload[6] = static_cast<uint8_t>((count >> 8) & 0xFF);

    uint8_t buf[64];
    BuildCmd(buf, CMD_HAPTIC_FEEDBACK, payload, static_cast<uint8_t>(sizeof(payload)));

    uint8_t outBuf[64] = {};
    outBuf[0] = 0x00; // HID output report ID 0
    outBuf[1] = CMD_HAPTIC_FEEDBACK;
    outBuf[2] = static_cast<uint8_t>(sizeof(payload));
    std::memcpy(outBuf + 3, payload, sizeof(payload));

    std::lock_guard<std::mutex> lock(m_featureMutex);
    bool featureOk = m_device.SendFeatureReport(buf, sizeof(buf));
    bool outputOk = m_device.SendOutputReport(outBuf, sizeof(outBuf));
    bool interruptOk = m_device.WriteOutputPacket(outPacket, sizeof(outPacket));

    logging::Logf(
        "[SteamController] Haptic command pos=%u amp=%u period=%u count=%u featureOk=%d outputOk=%d interruptOk=%d",
        static_cast<unsigned>(position),
        amplitude,
        period,
        count,
        featureOk ? 1 : 0,
        outputOk ? 1 : 0,
        interruptOk ? 1 : 0);

    return featureOk || outputOk || interruptOk;
}

void SteamController::RumbleLoop() {
    logging::Logf("[SteamController] RumbleLoop start");
    std::unique_lock<std::mutex> lock(m_rumbleMutex);

    while (!m_rumbleStop) {
        m_rumbleCv.wait(lock, [&] {
            return m_rumbleStop || m_largeMotor != 0 || m_smallMotor != 0;
        });

        while (!m_rumbleStop && (m_largeMotor != 0 || m_smallMotor != 0)) {
            const uint8_t largeMotor = m_largeMotor;
            const uint8_t smallMotor = m_smallMotor;
            lock.unlock();

            // The reverse-engineered packet uses 1 for left haptics and 0 for right.
            if (largeMotor != 0)
                SendHapticCommand(/*left=*/1, ScaleMotorToAmplitude(largeMotor), /*period=*/0, /*count=*/1);
            if (smallMotor != 0)
                SendHapticCommand(/*right=*/0, ScaleMotorToAmplitude(smallMotor), /*period=*/0, /*count=*/1);

            std::this_thread::sleep_for(std::chrono::milliseconds(16));
            lock.lock();
        }
    }

    logging::Logf("[SteamController] RumbleLoop stop");
}
