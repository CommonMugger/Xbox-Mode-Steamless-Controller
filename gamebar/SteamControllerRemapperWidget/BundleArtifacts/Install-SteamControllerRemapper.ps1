[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'

function Ensure-Elevated {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    if ($principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        return
    }

    $args = @(
        '-NoProfile',
        '-ExecutionPolicy', 'Bypass',
        '-File', $PSCommandPath
    )
    if ($Force) {
        $args += '-Force'
    }

    Write-Host 'Requesting administrator privileges...'
    Start-Process -FilePath 'powershell.exe' -ArgumentList $args -Verb RunAs -WindowStyle Normal | Out-Null
    exit 0
}

function Get-SingleFile([string]$Root, [string]$Pattern, [string]$Description) {
    $matches = Get-ChildItem -Path $Root -Filter $Pattern -File
    if ($matches.Count -eq 0) {
        throw "Could not find $Description matching '$Pattern' in $Root."
    }
    if ($matches.Count -gt 1) {
        throw "Found multiple $Description files matching '$Pattern' in $Root."
    }
    return $matches[0]
}

function Install-DesktopApp([string]$SourceExePath) {
    $installDir = Join-Path ${env:ProgramFiles} 'Steam Controller Remapper'
    $targetExePath = Join-Path $installDir 'Steam Controller Remapper.exe'

    Get-Process -Name 'Steam Controller Remapper' -ErrorAction SilentlyContinue | Stop-Process -Force
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
    Copy-Item -LiteralPath $SourceExePath -Destination $targetExePath -Force

    $startMenuDir = Join-Path ${env:ProgramData} 'Microsoft\Windows\Start Menu\Programs'
    $shortcutPath = Join-Path $startMenuDir 'Steam Controller Remapper.lnk'
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $targetExePath
    $shortcut.WorkingDirectory = $installDir
    $shortcut.IconLocation = $targetExePath
    $shortcut.Save()

    Write-Host "Installed desktop app to $installDir"
    return $targetExePath
}

function Enable-Startup([string]$InstalledExePath) {
    $runKeyPath = 'HKCU:\Software\Microsoft\Windows\CurrentVersion\Run'
    $valueName = 'Steam Controller Remapper'
    $command = '"' + $InstalledExePath + '"'

    if (-not (Test-Path $runKeyPath)) {
        New-Item -Path $runKeyPath -Force | Out-Null
    }

    Set-ItemProperty -Path $runKeyPath -Name $valueName -Value $command -Type String
    Write-Host 'Enabled Start with Windows for the current user.'
}

function Assert-WidgetInstalled([string]$PackageName) {
    $package = Get-AppxPackage -Name $PackageName -ErrorAction SilentlyContinue | Select-Object -First 1
    if (-not $package) {
        throw "Widget package '$PackageName' was not registered after install."
    }

    $manifest = Get-AppxPackageManifest -Package $package.PackageFullName
    $gameBarExtension = $manifest.Package.Applications.Application.Extensions.Extension |
        Where-Object { $_.Category -eq 'windows.appExtension' -and $_.AppExtension.Name -eq 'microsoft.gameBarUIExtension' } |
        Select-Object -First 1

    if (-not $gameBarExtension) {
        throw "Widget package '$PackageName' installed, but the Game Bar extension was not found in its manifest."
    }

    Write-Host "Verified widget package registration: $($package.PackageFullName)"
}

function Restart-GameBar {
    $processNames = @('GameBar', 'GameBarFTServer', 'XboxGameBarWidgets')
    foreach ($name in $processNames) {
        Get-Process -Name $name -ErrorAction SilentlyContinue | Stop-Process -Force
    }
    Write-Host 'Restarted Xbox Game Bar background processes. Reopen Game Bar with Win+G.'
}

function Install-WidgetPackage([string]$WidgetInstallerScriptPath) {
    Write-Host 'Installing widget package with Add-AppDevPackage.ps1...'
    & powershell.exe -NoProfile -ExecutionPolicy Bypass -File $WidgetInstallerScriptPath -Force
    if ($LASTEXITCODE -ne 0) {
        throw "Widget package installer failed with exit code $LASTEXITCODE."
    }
}

Ensure-Elevated

$bundleRoot = Split-Path -Parent $PSCommandPath
$desktopRoot = Join-Path $bundleRoot 'Desktop'
$widgetPackageRoot = Join-Path $bundleRoot 'WidgetPackage'

if (-not (Test-Path $desktopRoot)) {
    throw "Expected desktop app folder '$desktopRoot' was not found."
}
if (-not (Test-Path $widgetPackageRoot)) {
    throw "Expected widget package folder '$widgetPackageRoot' was not found."
}

$desktopExe = Get-SingleFile -Root $desktopRoot -Pattern '*.exe' -Description 'desktop executable'
$widgetInstallerScript = Join-Path $widgetPackageRoot 'Add-AppDevPackage.ps1'
if (-not (Test-Path $widgetInstallerScript)) {
    throw "Expected widget installer script '$widgetInstallerScript' was not found."
}

Write-Host 'Steam Controller Remapper installer'
Write-Host "Bundle root: $bundleRoot"

$installedExePath = Install-DesktopApp -SourceExePath $desktopExe.FullName
Enable-Startup -InstalledExePath $installedExePath

Import-BundleCertificate -CertificateFile $certificateFile

$packageName = 'SteamControllerRemapperWidget'
$installedPackage = Get-AppxPackage -Name $packageName -ErrorAction SilentlyContinue
if ($installedPackage) {
    Write-Host "Removing previous widget package: $($installedPackage.PackageFullName)"
    Remove-AppxPackage -Package $installedPackage.PackageFullName
}

Install-WidgetPackage -WidgetInstallerScriptPath $widgetInstallerScript
Assert-WidgetInstalled -PackageName $packageName
Restart-GameBar

Write-Host 'Launching Steam Controller Remapper...'
Start-Process -FilePath $installedExePath

Write-Host ''
Write-Host 'Install complete.'
Write-Host 'Next steps:'
Write-Host '1. Open Xbox Game Bar with Win+G.'
Write-Host '2. Open the Widgets menu.'
Write-Host '3. Add "Steam Controller Remapper".'
Write-Host '4. If you use Xbox Mode, confirm Windows shows it as a Startup app.'
