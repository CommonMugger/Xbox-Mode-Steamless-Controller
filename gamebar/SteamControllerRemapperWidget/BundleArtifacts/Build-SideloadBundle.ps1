[CmdletBinding()]
param(
    [string]$OutputFolder,
    [string]$ZipPath,
    [string]$CertificateThumbprint = 'C212808A52CEC7D75E312D7880CED6D6188A4BE1'
)

$ErrorActionPreference = 'Stop'

function Find-PackageFolder {
    $appPackagesRoot = Join-Path $PSScriptRoot '..\AppPackages'
    if (-not (Test-Path $appPackagesRoot)) {
        throw "AppPackages folder not found at $appPackagesRoot"
    }

    $candidates = Get-ChildItem -Path $appPackagesRoot -Directory | Where-Object {
        (Test-Path (Join-Path $_.FullName '*.msix')) -and
        (Test-Path (Join-Path $_.FullName 'Add-AppDevPackage.ps1'))
    }
    if ($candidates.Count -eq 0) {
        throw 'No built widget package folder was found under AppPackages.'
    }

    $preferred = $candidates | Where-Object { $_.Name -like '*_x64_*' }
    if ($preferred) {
        return $preferred | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    }
    return $candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1
}

function Export-PublicCertificate([string]$Thumbprint, [string]$DestinationPath) {
    $fallbackCertificate = Get-ChildItem -Path $PSScriptRoot -Recurse -Filter *.cer -File -ErrorAction SilentlyContinue |
        Sort-Object LastWriteTime -Descending |
        Select-Object -First 1

    $certificate =
        (Get-ChildItem Cert:\CurrentUser\My -ErrorAction SilentlyContinue | Where-Object { $_.Thumbprint -eq $Thumbprint } | Select-Object -First 1)
    if (-not $certificate) {
        $certificate =
            (Get-ChildItem Cert:\LocalMachine\My -ErrorAction SilentlyContinue | Where-Object { $_.Thumbprint -eq $Thumbprint } | Select-Object -First 1)
    }
    if ($certificate) {
        Export-Certificate -Cert $certificate -FilePath $DestinationPath -Force | Out-Null
        return
    }
    if ($fallbackCertificate) {
        Copy-Item -LiteralPath $fallbackCertificate.FullName -Destination $DestinationPath -Force
        return
    }
    if (-not $certificate) {
        throw "Certificate with thumbprint $Thumbprint was not found in CurrentUser\\My or LocalMachine\\My."
    }
}

function Find-DesktopExe {
    $repoRoot = Resolve-Path (Join-Path $scriptRoot '..\..\..')
    $desktopExePath = Join-Path $repoRoot 'build\release\Release\Steam Controller Remapper.exe'
    if (-not (Test-Path $desktopExePath)) {
        throw "Desktop executable not found at $desktopExePath. Build the Release target first."
    }
    return Get-Item -LiteralPath $desktopExePath
}

$scriptRoot = Split-Path -Parent $PSCommandPath
if ([string]::IsNullOrWhiteSpace($OutputFolder)) {
    $OutputFolder = Join-Path $scriptRoot 'SteamControllerRemapper-Installer'
}
if ([string]::IsNullOrWhiteSpace($ZipPath)) {
    $ZipPath = Join-Path $scriptRoot 'SteamControllerRemapper-Installer.zip'
}

$packageFolder = Find-PackageFolder
$desktopExe = Find-DesktopExe
if (-not (Test-Path (Join-Path $packageFolder.FullName 'Add-AppDevPackage.ps1'))) {
    throw "Add-AppDevPackage.ps1 was not found in $($packageFolder.FullName)"
}

if (Test-Path $OutputFolder) {
    Remove-Item -LiteralPath $OutputFolder -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputFolder | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutputFolder 'Desktop') | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutputFolder 'WidgetPackage') | Out-Null

Copy-Item -LiteralPath (Join-Path $packageFolder.FullName '*') -Destination (Join-Path $OutputFolder 'WidgetPackage') -Recurse
Copy-Item -LiteralPath $desktopExe.FullName -Destination (Join-Path $OutputFolder 'Desktop\Steam Controller Remapper.exe')
Copy-Item -LiteralPath (Join-Path $scriptRoot 'Install-SteamControllerRemapper.cmd') -Destination (Join-Path $OutputFolder 'Install-SteamControllerRemapper.cmd')
Copy-Item -LiteralPath (Join-Path $scriptRoot 'Install-SteamControllerRemapper.ps1') -Destination (Join-Path $OutputFolder 'Install-SteamControllerRemapper.ps1')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $OutputFolder 'README.md')

$certificatePath = Join-Path $OutputFolder 'SteamControllerRemapperWidget.cer'
Export-PublicCertificate -Thumbprint $CertificateThumbprint -DestinationPath $certificatePath

if (Test-Path $ZipPath) {
    Remove-Item -LiteralPath $ZipPath -Force
}
Compress-Archive -Path (Join-Path $OutputFolder '*') -DestinationPath $ZipPath -Force

Write-Host "Created installer bundle at:"
Write-Host $OutputFolder
Write-Host "Created installer archive at:"
Write-Host $ZipPath
