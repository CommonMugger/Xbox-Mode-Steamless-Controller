#include "VirtualController.h"
#include "logging/Log.h"
#include "steam/SteamController.h"
#include <ViGEmClient.h>
#include <algorithm>
#include <cstdio>
#include <mutex>
#include <unordered_map>

// ---------------------------------------------------------------------------
// Report translation — 0x42 → XUSB_REPORT
// ---------------------------------------------------------------------------

static XUSB_REPORT Translate(const uint8_t* buf, size_t n) {
    XUSB_REPORT r{};
    if (n < 18) return r;

    const uint8_t b0 = buf[2];
    const uint8_t b1 = buf[3];
    const uint8_t b2 = buf[4];

    // Face buttons
    if (b0 & SteamController::BTN_A) r.wButtons |= XUSB_GAMEPAD_A;
    if (b0 & SteamController::BTN_B) r.wButtons |= XUSB_GAMEPAD_B;
    if (b0 & SteamController::BTN_X) r.wButtons |= XUSB_GAMEPAD_X;
    if (b0 & SteamController::BTN_Y) r.wButtons |= XUSB_GAMEPAD_Y;

    // Bumpers
    if (b2 & SteamController::BTN_LB) r.wButtons |= XUSB_GAMEPAD_LEFT_SHOULDER;
    if (b1 & SteamController::BTN_RB) r.wButtons |= XUSB_GAMEPAD_RIGHT_SHOULDER;

    // Menu / View (Start / Back)
    if (b0 & SteamController::BTN_MENU) r.wButtons |= XUSB_GAMEPAD_START;
    if (b1 & SteamController::BTN_VIEW) r.wButtons |= XUSB_GAMEPAD_BACK;

    // Stick clicks
    if (b1 & SteamController::BTN_LS) r.wButtons |= XUSB_GAMEPAD_LEFT_THUMB;
    if (b0 & SteamController::BTN_RS) r.wButtons |= XUSB_GAMEPAD_RIGHT_THUMB;

    // Steam / Guide button
    if (b2 & SteamController::BTN_STEAM) r.wButtons |= XUSB_GAMEPAD_GUIDE;

    // D-pad
    if (b1 & SteamController::BTN_DPAD_UP)  r.wButtons |= XUSB_GAMEPAD_DPAD_UP;
    if (b1 & SteamController::BTN_DPAD_DN)  r.wButtons |= XUSB_GAMEPAD_DPAD_DOWN;
    if (b1 & SteamController::BTN_DPAD_LT)  r.wButtons |= XUSB_GAMEPAD_DPAD_LEFT;
    if (b1 & SteamController::BTN_DPAD_RT)  r.wButtons |= XUSB_GAMEPAD_DPAD_RIGHT;

    // Triggers: 16-bit signed (0x0000–0x7FFF) → 8-bit (0–255)
    int16_t ltRaw, rtRaw;
    memcpy(&ltRaw, buf + 6, 2);
    memcpy(&rtRaw, buf + 8, 2);
    r.bLeftTrigger  = static_cast<uint8_t>(std::clamp<int>(ltRaw >> 7, 0, 255));
    r.bRightTrigger = static_cast<uint8_t>(std::clamp<int>(rtRaw >> 7, 0, 255));

    // Sticks: 16-bit signed, same range as XInput — pass through directly
    memcpy(&r.sThumbLX, buf + 10, 2);
    memcpy(&r.sThumbLY, buf + 12, 2);
    memcpy(&r.sThumbRX, buf + 14, 2);
    memcpy(&r.sThumbRY, buf + 16, 2);

    return r;
}

// ---------------------------------------------------------------------------
// VirtualController
// ---------------------------------------------------------------------------

namespace {
std::mutex g_notificationMutex;
std::unordered_map<void*, VirtualController*> g_targetOwners;
}

VirtualController::VirtualController(RumbleCallback onRumble)
    : m_onRumble(std::move(onRumble)) {
    logging::Logf("[ViGEm] VirtualController ctor");
    m_client = vigem_alloc();
    if (!m_client) {
        printf("[ViGEm] alloc failed\n");
        logging::Logf("[ViGEm] alloc failed");
        return;
    }

    VIGEM_ERROR err = vigem_connect(static_cast<PVIGEM_CLIENT>(m_client));
    if (!VIGEM_SUCCESS(err)) {
        logging::Logf("[ViGEm] connect failed err=0x%08X", err);
        if (err == VIGEM_ERROR_BUS_NOT_FOUND || err == VIGEM_ERROR_BUS_ACCESS_FAILED)
            m_driverMissing = true;
        vigem_free(static_cast<PVIGEM_CLIENT>(m_client));
        m_client = nullptr;
        return;
    }

    m_target = vigem_target_x360_alloc();
    if (!m_target) {
        printf("[ViGEm] target alloc failed\n");
        logging::Logf("[ViGEm] target alloc failed");
        return;
    }

    err = vigem_target_add(static_cast<PVIGEM_CLIENT>(m_client),
                           static_cast<PVIGEM_TARGET>(m_target));
    if (!VIGEM_SUCCESS(err)) {
        printf("[ViGEm] target_add failed: 0x%08X\n", err);
        logging::Logf("[ViGEm] target_add failed err=0x%08X", err);
        vigem_target_free(static_cast<PVIGEM_TARGET>(m_target));
        m_target = nullptr;
        return;
    }

    printf("[ViGEm] Virtual Xbox 360 controller connected\n");
    logging::Logf("[ViGEm] Virtual Xbox 360 controller connected");

    err = vigem_target_x360_register_notification(
        static_cast<PVIGEM_CLIENT>(m_client),
        static_cast<PVIGEM_TARGET>(m_target),
        reinterpret_cast<PFN_VIGEM_X360_NOTIFICATION>(&VirtualController::ViGEmNotification));
    if (!VIGEM_SUCCESS(err)) {
        logging::Logf("[ViGEm] register notification failed err=0x%08X", err);
    } else {
        std::lock_guard<std::mutex> lock(g_notificationMutex);
        g_targetOwners[m_target] = this;
    }

    m_valid = true;
}

VirtualController::~VirtualController() {
    logging::Logf("[ViGEm] VirtualController dtor valid=%d", m_valid ? 1 : 0);
    if (m_target) {
        {
            std::lock_guard<std::mutex> lock(g_notificationMutex);
            g_targetOwners.erase(m_target);
        }
        vigem_target_x360_unregister_notification(static_cast<PVIGEM_TARGET>(m_target));
    }
    if (m_client && m_target) {
        vigem_target_remove(static_cast<PVIGEM_CLIENT>(m_client),
                            static_cast<PVIGEM_TARGET>(m_target));
    }
    if (m_target) vigem_target_free(static_cast<PVIGEM_TARGET>(m_target));
    if (m_client) {
        vigem_disconnect(static_cast<PVIGEM_CLIENT>(m_client));
        vigem_free(static_cast<PVIGEM_CLIENT>(m_client));
    }
}

void VirtualController::Update(const uint8_t* buf, size_t n) {
    if (!m_valid) return;
    XUSB_REPORT report = Translate(buf, n);
    vigem_target_x360_update(static_cast<PVIGEM_CLIENT>(m_client),
                             static_cast<PVIGEM_TARGET>(m_target),
                             report);
}

void VirtualController::ViGEmNotification(void* client, void* target, uint8_t largeMotor, uint8_t smallMotor, uint8_t ledNumber) {
    (void)client;
    (void)ledNumber;

    std::lock_guard<std::mutex> lock(g_notificationMutex);
    auto it = g_targetOwners.find(target);
    if (it == g_targetOwners.end() || !it->second->m_onRumble)
        return;

    it->second->m_onRumble(largeMotor, smallMotor);
}
