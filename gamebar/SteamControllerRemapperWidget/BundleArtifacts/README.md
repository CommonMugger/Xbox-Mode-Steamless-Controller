# Steam Controller Remapper Installer Bundle

This folder is the single installer bundle for both:

- the desktop remapper app
- the Xbox Game Bar widget

## Contents

- `Install-SteamControllerRemapper.cmd`
- `Install-SteamControllerRemapper.ps1`
- `Desktop\Steam Controller Remapper.exe`
- `SteamControllerRemapperWidget.cer`
- `SteamControllerRemapperWidget_*.msix`
- `Dependencies\x64\*.appx`

## End-user install

1. Extract the bundle zip.
2. Double-click `Install-SteamControllerRemapper.cmd`.
3. It will launch PowerShell with `-ExecutionPolicy Bypass`.
4. If it is not already running as administrator, it will relaunch itself elevated. Accept the elevation prompt.
5. Let the script install the desktop app, enable Start with Windows, install the widget, and refresh Xbox Game Bar.
6. Open Xbox Game Bar with `Win + G`.
7. Add the `Steam Controller Remapper` widget from the Widgets menu.

## Rebuilding the bundle

Run this from the repo after rebuilding the widget package:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\gamebar\SteamControllerRemapperWidget\BundleArtifacts\Build-SideloadBundle.ps1
```

The script stages a fresh installer folder in:

`gamebar\SteamControllerRemapperWidget\BundleArtifacts\SteamControllerRemapper-Installer`

and a matching archive:

`gamebar\SteamControllerRemapperWidget\BundleArtifacts\SteamControllerRemapper-Installer.zip`
