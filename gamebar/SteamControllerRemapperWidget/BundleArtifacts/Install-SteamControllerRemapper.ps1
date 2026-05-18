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
        '-ExecutionPolicy', 'Bypass',
        '-File', ('"' + $PSCommandPath + '"')
    )
    if ($Force) {
        $args += '-Force'
    }

    Start-Process -FilePath 'powershell.exe' -ArgumentList $args -Verb RunAs | Out-Null
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

function Import-BundleCertificate([System.IO.FileInfo]$CertificateFile) {
    $certificate = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2($CertificateFile.FullName)
    $thumbprint = $certificate.Thumbprint
    $existing = Get-ChildItem Cert:\LocalMachine\TrustedPeople | Where-Object { $_.Thumbprint -eq $thumbprint }
    if ($existing) {
        Write-Host "Certificate already trusted: $thumbprint"
        return
    }

    Write-Host "Importing widget certificate into LocalMachine\\TrustedPeople..."
    Import-Certificate -FilePath $CertificateFile.FullName -CertStoreLocation Cert:\LocalMachine\TrustedPeople | Out-Null
}

function Install-Dependencies([string[]]$DependencyPaths) {
    foreach ($dependencyPath in $DependencyPaths) {
        Write-Host "Installing dependency $(Split-Path $dependencyPath -Leaf)..."
        Add-AppxPackage -Path $dependencyPath -ErrorAction Stop
    }
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

Ensure-Elevated

$bundleRoot = Split-Path -Parent $PSCommandPath
$desktopRoot = Join-Path $bundleRoot 'Desktop'
$dependencyRoot = Join-Path $bundleRoot 'Dependencies\x64'

if (-not (Test-Path $desktopRoot)) {
    throw "Expected desktop app folder '$desktopRoot' was not found."
}
if (-not (Test-Path $dependencyRoot)) {
    throw "Expected dependency folder '$dependencyRoot' was not found."
}

$desktopExe = Get-SingleFile -Root $desktopRoot -Pattern '*.exe' -Description 'desktop executable'
$widgetPackage = Get-SingleFile -Root $bundleRoot -Pattern '*.msix' -Description 'widget package'
$certificateFile = Get-SingleFile -Root $bundleRoot -Pattern '*.cer' -Description 'certificate'
$dependencyPaths = Get-ChildItem -Path $dependencyRoot -Filter *.appx -File | Sort-Object Name | Select-Object -ExpandProperty FullName
if (-not $dependencyPaths) {
    throw "No dependency .appx files were found in '$dependencyRoot'."
}

Write-Host 'Steam Controller Remapper installer'
Write-Host "Bundle root: $bundleRoot"

$installedExePath = Install-DesktopApp -SourceExePath $desktopExe.FullName

Import-BundleCertificate -CertificateFile $certificateFile

$packageName = 'SteamControllerRemapperWidget'
$installedPackage = Get-AppxPackage -Name $packageName -ErrorAction SilentlyContinue
if ($installedPackage) {
    Write-Host "Removing previous widget package: $($installedPackage.PackageFullName)"
    Remove-AppxPackage -Package $installedPackage.PackageFullName
}

Install-Dependencies -DependencyPaths $dependencyPaths

Write-Host "Installing widget package $(Split-Path $widgetPackage.FullName -Leaf)..."
Add-AppxPackage -Path $widgetPackage.FullName -DependencyPath $dependencyPaths

Write-Host 'Launching Steam Controller Remapper...'
Start-Process -FilePath $installedExePath

Write-Host ''
Write-Host 'Install complete.'
Write-Host 'Next steps:'
Write-Host '1. Open Xbox Game Bar with Win+G.'
Write-Host '2. Open the Widgets menu.'
Write-Host '3. Add "Steam Controller Remapper".'
