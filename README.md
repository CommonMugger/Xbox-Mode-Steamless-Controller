# Xbox Mode Steamless Controller

A lightweight Windows system tray app that lets you use a **Steam Controller** as a standard gamepad without Steam running.

This fork exists to make the original project reliable in **Xbox Mode**. It focuses on boot-time controller recovery, delayed HID availability after reboot, and clean handoff back to Steam Input when `steam.exe` launches.

<img width="261" height="194" alt="image" src="https://github.com/user-attachments/assets/8e4a1355-d854-4b67-a486-590d225700f5" />

When **Steamless Mode** is active, the app disables the controller's built-in keyboard/mouse emulation (lizard mode) and exposes it as a virtual Xbox 360 controller via [ViGEmBus](https://github.com/nefarius/ViGEmBus), making it compatible with any game that supports XInput or the Xbox controller.

By default, the tray app automatically enables Steamless Mode when the controller is available and Steam is not running. If Steam launches, the app restores lizard mode and tears down the virtual Xbox controller so Steam Input can take ownership. When Steam closes, Steamless Mode comes back automatically.

## Features

- System tray icon shows connection and mode status
- **Steamless Mode** disables lizard mode and exposes the controller as an Xbox 360 gamepad
- **Auto-enable Steamless Mode** restores the virtual controller automatically when Steam is closed
- **Steam-aware handoff** disables Steamless Mode while `steam.exe` is running, then re-enables it when Steam exits
- **Xbox Mode recovery** retries controller discovery after boot until the Steam Controller HID interface is actually ready
- **Trackpad Mouse** uses the right or left trackpad as a mouse cursor
- **Back Buttons for Clicking** maps R4/R5 or L4/L5 to left and right mouse click
- **Use Left Trackpad Instead** mirrors all trackpad/back-button functionality to the left side
- **Start with Windows** launches automatically at login
- Settings persist across restarts
- Single-instance guard so it is safe to leave running

<img width="482" height="302" alt="image" src="https://github.com/user-attachments/assets/62e274a5-9d23-4af2-aaca-0f3ecdca3feb" />

## Requirements

### To run
- Windows 10 or later (64-bit)
- [ViGEmBus](https://github.com/nefarius/ViGEmBus/releases/latest) driver installed
- Steam Controller (VID `0x28DE` / PID `0x1302`)
- Steam may be running, but Steamless Mode will stay off until `steam.exe` exits

### To build
- [Visual Studio 2022](https://visualstudio.microsoft.com/) with the **Desktop development with C++** workload
- [CMake](https://cmake.org/download/) 3.20 or later
- Windows SDK 10.0.22000 or later

## Building

```bat
git clone https://github.com/CommonMugger/SteamlessController.git
cd SteamlessController
cmake -B build
cmake --build build --target SteamlessController
```

The debug executable will be at `build\Debug\Xbox Mode Steamless Controller.exe`.

For a release build:

```bat
cmake -B build/release -G "Visual Studio 17 2022"
cmake --build build/release --config Release --target SteamlessController
```

## CMake Targets

| Target | Description |
|---|---|
| `SteamlessController` | Main system tray application |
| `SteamProbe` | Console diagnostic tool that dumps raw HID report bytes while you interact with the controller |
| `RawControllerProbe` | Checks whether `Windows.Gaming.Input.RawGameController` can enumerate the Steam Controller |

## How It Works

The Steam Controller exposes a vendor HID collection (usage page `0xFF00`) that carries all game input in a 54-byte report (ID `0x42`) at roughly 60 Hz. By default the firmware runs in **lizard mode**, emulating a keyboard and mouse so the controller works without drivers.

Xbox Mode Steamless Controller sends HID feature reports to disable lizard mode, then reads the raw input reports and translates them into a virtual Xbox 360 controller via ViGEmBus. A background heartbeat re-sends the disable command every 800 ms to keep lizard mode off.

The full input report layout is documented in [`src/steam/SteamController.h`](src/steam/SteamController.h).

## Third-party

- [ViGEmClient](https://github.com/nefarius/ViGEmClient) - MIT License, built from source as a static library
