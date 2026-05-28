[CmdletBinding()]
param(
    [switch]$Force
)

$ErrorActionPreference = 'Stop'
$LogPath = Join-Path $env:TEMP 'SteamControllerRemapper-Installer.log'

function Write-InstallerLog([string]$Message) {
    $line = ('{0} {1}' -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss.fff'), $Message)
    Add-Content -LiteralPath $LogPath -Value $line
    Write-Host $Message
}

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

    Write-InstallerLog 'Requesting administrator privileges...'
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

function Test-UsbIpDriverPresent {
    $services = @(
        (Get-Service -Name 'usbip_vhci' -ErrorAction SilentlyContinue),
        (Get-Service -Name 'usbip2_vhci' -ErrorAction SilentlyContinue)
    ) | Where-Object { $_ }
    if ($services) {
        return $true
    }

    $driverText = & pnputil.exe /enum-drivers 2>$null | Out-String
    return ($driverText -match 'usbip' -or $driverText -match 'vhci')
}

function Get-LatestUsbIpInstaller {
    $release = Invoke-RestMethod -Headers @{ 'User-Agent' = 'SteamControllerRemapperInstaller' } `
        -Uri 'https://api.github.com/repos/vadimgrn/usbip-win2/releases/latest'
    $asset = $release.assets |
        Where-Object { $_.name -match '^USBip-.*-x64\.exe$' } |
        Select-Object -First 1
    if (-not $asset) {
        throw 'Could not find an x64 usbip-win2 installer asset in the latest GitHub release.'
    }

    return [pscustomobject]@{
        Tag = $release.tag_name
        Name = $asset.name
        Url = $asset.browser_download_url
    }
}

function Install-UsbIp {
    if (Test-UsbIpDriverPresent) {
        Write-InstallerLog 'USBIP driver already present. Skipping usbip-win2 installation.'
        return
    }

    $asset = Get-LatestUsbIpInstaller
    $downloadDir = Join-Path $env:TEMP 'SteamControllerRemapper-Installer'
    New-Item -ItemType Directory -Path $downloadDir -Force | Out-Null
    $installerPath = Join-Path $downloadDir $asset.Name

    Write-InstallerLog "Downloading usbip-win2 $($asset.Tag) from $($asset.Url)"
    Invoke-WebRequest -Headers @{ 'User-Agent' = 'SteamControllerRemapperInstaller' } `
        -Uri $asset.Url -OutFile $installerPath

    $installLogPath = Join-Path $downloadDir 'usbip-win2-install.log'
    $arguments = @('/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', "/LOG=`"$installLogPath`"")
    Write-InstallerLog "Installing usbip-win2 from $installerPath"
    $process = Start-Process -FilePath $installerPath -ArgumentList $arguments -Wait -PassThru
    if ($process.ExitCode -ne 0) {
        throw "usbip-win2 installer failed with exit code $($process.ExitCode). See $installLogPath"
    }

    Start-Sleep -Seconds 2
    if (-not (Test-UsbIpDriverPresent)) {
        throw 'usbip-win2 installer completed, but the USBIP driver was still not detected. A reboot may be required.'
    }

    Write-InstallerLog "Installed usbip-win2 $($asset.Tag)"
}

function Install-DesktopApp([string]$DesktopSourcePath) {
    $installDir = Join-Path ${env:ProgramFiles} 'Steam Controller Remapper'
    $targetExePath = Join-Path $installDir 'Steam Controller Remapper.exe'

    Get-Process -Name 'Steam Controller Remapper' -ErrorAction SilentlyContinue | Stop-Process -Force
    New-Item -ItemType Directory -Path $installDir -Force | Out-Null
    Copy-Item -Path (Join-Path $DesktopSourcePath '*') -Destination $installDir -Recurse -Force

    $startMenuDir = Join-Path ${env:ProgramData} 'Microsoft\Windows\Start Menu\Programs'
    $shortcutPath = Join-Path $startMenuDir 'Steam Controller Remapper.lnk'
    $shell = New-Object -ComObject WScript.Shell
    $shortcut = $shell.CreateShortcut($shortcutPath)
    $shortcut.TargetPath = $targetExePath
    $shortcut.WorkingDirectory = $installDir
    $shortcut.IconLocation = $targetExePath
    $shortcut.Save()

    Write-InstallerLog "Installed desktop app to $installDir"
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
    Write-InstallerLog 'Enabled Start with Windows for the current user.'
}

function Import-BundleCertificate([string]$CertificateFile) {
    if (-not (Test-Path $CertificateFile)) {
        throw "Expected certificate file '$CertificateFile' was not found."
    }

    $certificate = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($CertificateFile)
    $existing = Get-ChildItem -Path Cert:\CurrentUser\TrustedPeople -ErrorAction SilentlyContinue |
        Where-Object { $_.Thumbprint -eq $certificate.Thumbprint } |
        Select-Object -First 1
    if ($existing) {
        Write-InstallerLog "Widget certificate already trusted: $($certificate.Thumbprint)"
        return
    }

    Import-Certificate -FilePath $CertificateFile -CertStoreLocation 'Cert:\CurrentUser\TrustedPeople' | Out-Null
    Write-InstallerLog "Imported widget certificate to CurrentUser\\TrustedPeople: $($certificate.Thumbprint)"
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

    Write-InstallerLog "Verified widget package registration: $($package.PackageFullName)"
}

function Restart-GameBar {
    $processNames = @('GameBar', 'GameBarFTServer', 'XboxGameBarWidgets')
    foreach ($name in $processNames) {
        Get-Process -Name $name -ErrorAction SilentlyContinue | Stop-Process -Force
    }
    Write-InstallerLog 'Restarted Xbox Game Bar background processes. Reopen Game Bar with Win+G.'
}

function Install-WidgetPackage([string]$WidgetInstallerScriptPath) {
    Write-InstallerLog 'Installing widget package with Add-AppDevPackage.ps1...'
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

$desktopExe = Get-SingleFile -Root $desktopRoot -Pattern 'Steam Controller Remapper.exe' -Description 'desktop executable'
$desktopViiperDll = Join-Path $desktopRoot 'libVIIPER.dll'
$widgetInstallerScript = Join-Path $widgetPackageRoot 'Add-AppDevPackage.ps1'
$certificateFile = Join-Path $widgetPackageRoot 'SteamControllerRemapperWidget.cer'
if (-not (Test-Path $widgetInstallerScript)) {
    throw "Expected widget installer script '$widgetInstallerScript' was not found."
}
if (-not (Test-Path $desktopViiperDll)) {
    throw "Expected libVIIPER.dll at '$desktopViiperDll' was not found."
}

Write-InstallerLog 'Steam Controller Remapper installer'
Write-InstallerLog "Bundle root: $bundleRoot"

Install-UsbIp

$installedExePath = Install-DesktopApp -DesktopSourcePath $desktopRoot
Enable-Startup -InstalledExePath $installedExePath

Import-BundleCertificate -CertificateFile $certificateFile

$packageName = 'SteamControllerRemapperWidget'
$installedPackage = Get-AppxPackage -Name $packageName -ErrorAction SilentlyContinue
if ($installedPackage) {
    Write-InstallerLog "Removing previous widget package: $($installedPackage.PackageFullName)"
    Remove-AppxPackage -Package $installedPackage.PackageFullName
}

try {
    Install-WidgetPackage -WidgetInstallerScriptPath $widgetInstallerScript
    Assert-WidgetInstalled -PackageName $packageName
    Restart-GameBar

    Write-InstallerLog 'Launching Steam Controller Remapper...'
    Start-Process -FilePath $installedExePath

    Write-InstallerLog 'Install complete.'
    Write-InstallerLog 'Next steps: Open Xbox Game Bar with Win+G, open the Widgets menu, and add Steam Controller Remapper.'
} catch {
    Write-InstallerLog ("Install failed: " + $_.Exception.Message)
    throw
}
