# Steam Controller Remapper

Steam Controller Remapper is a Windows tray app for using a **Steam Controller** without Steam Input taking over. It disables lizard mode, uses **SDL** for standard controller input, exposes the controller through **VIIPER/libVIIPER** virtual output, and layers game-aware paddle profiles on top.

It now has two editing surfaces:

- a full desktop remapper for profile setup, sources, macros, and controller-first editing
- an Xbox Game Bar widget for in-session profile switching and quick paddle remaps

## At a glance

### Desktop remapper

![Desktop remapper](docs/images/remapper.png)

The desktop editor is the full configuration surface. Use it to:

- build and edit per-game profiles
- add multiple game library folders or direct game `.exe` sources
- refresh and cache the installed-game list
- create gamepad, shortcut, macro, or unbound paddle actions
- enable rapid fire where it applies
- use controller navigation instead of relying on mouse-only interaction

### Xbox Game Bar widget

![Game Bar widget](docs/images/gamebar-widget.png)

The widget is the quick-access surface for Xbox Mode and in-game use. Use it to:

- see the active profile and detected game
- toggle auto-switch
- switch to another installed-game profile
- make fast gamepad-button remaps for `L4`, `L5`, `R4`, `R5`, and `QAM`

Current widget limitation:

- Game Bar quick remaps currently edit only gamepad-button mappings. Choosing a value there will overwrite any key chord or macro on that paddle.

### Tray icon and quick controls

![Tray icon menu](docs/images/tray-icon.png)

The tray icon is the quickest way to manage the remapper without opening the full editor. From the menu you can:

- toggle Steamless Mode and auto-enable behavior
- switch controller emulation mode
- open `Remap Buttons...`
- toggle trackpad mouse behavior
- toggle `Start with Windows`
- exit the app cleanly

## Feature set

- Uses SDL for standard Steam Controller input handling
- Uses VIIPER/libVIIPER for virtual Xbox 360 or DualShock 4 output
- Automatically backs off when Steam is running so Steam Input can take over
- Auto-switches profiles when a matching game launches, then returns to `Default` when that game closes
- Supports Steam libraries, Xbox/Game Pass installs, broad game folders, and direct `.exe` sources
- Persists profiles, library sources, and widget state between launches
- Includes an Xbox Game Bar widget for live profile switching and quick remaps
- Includes controller-friendly navigation in the desktop remapper and input capture dialogs
- Starts from the tray and is safe to leave running

## Install

### Recommended release install

1. Download the latest release.
2. Extract `SteamControllerRemapper-Installer.zip`.
3. Run `Install-SteamControllerRemapper.cmd`.
4. Launch `Steam Controller Remapper`.
5. Open Xbox Game Bar with `Win + G` and add the `Steam Controller Remapper` widget from the widgets menu.

What that installer does:

- downloads and installs the latest upstream `usbip-win2` package when the USBIP driver is not already present
- installs the desktop app into `Program Files`
- enables `Start with Windows` for the current user
- imports the widget certificate
- ships the staged desktop executable signed with the same local release certificate used for the widget bundle
- installs the Game Bar widget and dependencies
- creates a Start Menu shortcut
- bypasses PowerShell execution policy for the installer launch

Windows trust notes for this release:

- UAC prompts during install are expected
- the widget install path uses a bundled local certificate, so Windows will ask you to trust that certificate for sideloading
- this project does not use a paid public code-signing certificate

Upgrade note for existing users:

- if you are upgrading from the old ViGEmBus-based release, uninstall the old version first, then install this release fresh
- the tray app's `Check for Updates...` action now downloads the latest `SteamControllerRemapper-Installer.zip` from GitHub Releases and launches the bundled installer automatically

### Desktop-only install

If you do not want the widget, you can run `Steam Controller Remapper.exe` by itself after extraction.

## Xbox Mode startup

For reliable Xbox Mode startup, Windows Startup Apps should be set to **System startup** for `Steam Controller Remapper`, not **Desktop startup**.

1. Open **Settings > Apps > Startup**.
2. Find `Steam Controller Remapper`.
3. Change its startup mode to **System startup**.

## Requirements

- Windows 10 or later, 64-bit
- Steam Controller
- Xbox Game Bar, if you want the widget
- Administrator rights during install, because `usbip-win2` and the desktop app install into system locations

## Supported controller path

- Wired Steam Controller
- Steam Controller through the wireless dongle
- VIIPER virtual output through `libVIIPER.dll`
- USBIP support provided by the latest upstream `usbip-win2` installer at install time

## Trackpad behavior

- Cursor movement uses the app's host-side trackpad path, with SDL used for standard gamepad state and raw controller reports used for trackpad motion where SDL exposes no touchpad data
- Clicks use the opposite trackpad:
  - short press = left click
  - hold = right click
- Clicks send a short haptic pulse at click commit time

## Known limitations

- Trackpad feel is tuned toward Steam desktop behavior, but exact one-to-one parity with Steam Input is not guaranteed
- Xbox Game Bar quick remaps only edit gamepad-button mappings
- The widget install path still requires Windows sideloading trust for the bundled certificate
- Windows may show SmartScreen or other trust friction because the release is not signed with a paid public code-signing certificate

## Build

```powershell
git clone https://github.com/CommonMugger/Steam-Controller-Remapper.git
cd Steam-Controller-Remapper
cmake --preset release
cmake --build --preset release
```

Release output:

- `build\release\Release\Steam Controller Remapper.exe`

To rebuild the Game Bar sideload bundle after the widget package exists:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\gamebar\SteamControllerRemapperWidget\BundleArtifacts\Build-SideloadBundle.ps1
```

## SDL dependency automation

- SDL is pinned from `cmake/SDLVersion.cmake` and fetched through CMake `FetchContent`.
- GitHub Actions checks for new SDL releases on a schedule.
- Patch and minor SDL updates are opened as `[SDL]` PRs and set to auto-merge after CI passes.
- Major SDL updates are opened for manual review.

## VIIPER and USBIP automation

- Vendored `libVIIPER.dll` and `libVIIPER.h` are tracked in `third_party/libVIIPER`.
- GitHub Actions checks upstream `VIIPER` and `usbip-win2` releases on a schedule.
- The workflow refreshes `third_party/libVIIPER` from the latest Windows `libVIIPER` release and updates `cmake/VirtualBusVersions.cmake`.
- The installer still downloads the latest upstream `usbip-win2` release at install time, so end-user installs do not wait on a repo refresh to pick up USBIP fixes.

## License

This project is now distributed under **GPL-3.0-or-later** because it links against **libVIIPER**.

Original SteamlessController attribution and the original MIT notice are preserved in `LICENSES/original-steamlesscontroller-mit.txt`.

## Credits

- Original project: [ddeverill/SteamlessController](https://github.com/ddeverill/SteamlessController)
- Original author: [ddeverill](https://github.com/ddeverill)
