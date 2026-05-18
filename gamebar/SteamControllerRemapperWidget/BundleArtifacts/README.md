# Steam Controller Remapper Sideload Bundle

This folder is a GitHub-style sideload package for both:

- the desktop remapper app
- the Xbox Game Bar widget

## Contents

- `Install-SteamControllerRemapper.ps1`
- `Desktop\Steam Controller Remapper.exe`
- `SteamControllerRemapperWidget.cer`
- `SteamControllerRemapperWidget_*.msix`
- `Dependencies\x64\*.appx`

## End-user install

1. Extract the bundle zip.
2. Right-click `Install-SteamControllerRemapper.ps1`.
3. Choose `Run with PowerShell`.
4. Accept the elevation prompt.
5. Let the script install the desktop app and widget.
6. Open Xbox Game Bar with `Win + G`.
7. Add the `Steam Controller Remapper` widget from the Widgets menu.

## Rebuilding the bundle

Run this from the repo after rebuilding the widget package:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\gamebar\SteamControllerRemapperWidget\BundleArtifacts\Build-SideloadBundle.ps1
```

The script stages a fresh bundle in:

`gamebar\SteamControllerRemapperWidget\BundleArtifacts\SteamControllerRemapperWidget-Sideload`
