# Steam Controller Remapper Widget

This is the Xbox Game Bar widget for Steam Controller Remapper.

## Current Scope

- show the active profile
- show the currently detected game
- list installed game profiles
- apply a selected profile
- toggle auto-switch
- quick-edit gamepad-button bindings for `L4`, `L5`, `R4`, `R5`, and `QAM`

## Desktop Bridge

The widget talks to the desktop app through the package `LocalState` bridge, not a direct pipe.

Desktop app publishes:

- `widget-state.json`

Widget sends:

- `widget-request.txt`

Desktop app replies with:

- `widget-response.txt`

## Current Widget Commands

- `APPLY_PROFILE`
- `SET_AUTO_SWITCH`
- `REFRESH_LIBRARY`
- `SET_PROFILE_GAMEPAD_BINDING`

## Build Command

```powershell
msbuild .\gamebar\SteamControllerRemapperWidget\SteamControllerRemapperWidget.csproj `
  /restore /t:Build /p:Configuration=Debug /p:Platform=x64 `
  /p:AppxBundle=Never /p:UapAppxPackageBuildMode=SideloadOnly `
  /p:AppxPackageDir=.\gamebar\SteamControllerRemapperWidget\AppPackages\
```

## Sideload Bundle

For GitHub-style distribution without Microsoft Store publishing, use the bundle scripts in `gamebar\SteamControllerRemapperWidget\BundleArtifacts`.

Create a fresh distributable bundle with:

```powershell
powershell.exe -ExecutionPolicy Bypass -File .\gamebar\SteamControllerRemapperWidget\BundleArtifacts\Build-SideloadBundle.ps1
```

That produces a folder containing:

- the signed widget `.msix`
- the `Dependencies` folder
- an exported `.cer`
- the desktop `Steam Controller Remapper.exe`
- `Install-SteamControllerRemapper.ps1`

End users can then extract that bundle and run `Install-SteamControllerRemapper.ps1` as administrator, similar to the GoTweaks install flow.
