[CmdletBinding()]
param(
    [string]$OutputFolder,
    [string]$CertificateThumbprint = 'C212808A52CEC7D75E312D7880CED6D6188A4BE1'
)

$ErrorActionPreference = 'Stop'

function Find-PackageFolder {
    $appPackagesRoot = Join-Path $PSScriptRoot '..\AppPackages'
    if (-not (Test-Path $appPackagesRoot)) {
        throw "AppPackages folder not found at $appPackagesRoot"
    }

    $candidates = Get-ChildItem -Path $appPackagesRoot -Directory | Where-Object {
        Test-Path (Join-Path $_.FullName '*.msix')
    }
    if ($candidates.Count -eq 0) {
        throw 'No built widget package folder was found under AppPackages.'
    }

    return $candidates | Sort-Object LastWriteTime -Descending | Select-Object -First 1
}

function Export-PublicCertificate([string]$Thumbprint, [string]$DestinationPath) {
    $certificate =
        (Get-ChildItem Cert:\CurrentUser\My -ErrorAction SilentlyContinue | Where-Object { $_.Thumbprint -eq $Thumbprint } | Select-Object -First 1)
    if (-not $certificate) {
        $certificate =
            (Get-ChildItem Cert:\LocalMachine\My -ErrorAction SilentlyContinue | Where-Object { $_.Thumbprint -eq $Thumbprint } | Select-Object -First 1)
    }
    if (-not $certificate) {
        throw "Certificate with thumbprint $Thumbprint was not found in CurrentUser\\My or LocalMachine\\My."
    }

    Export-Certificate -Cert $certificate -FilePath $DestinationPath -Force | Out-Null
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
    $OutputFolder = Join-Path $scriptRoot 'SteamControllerRemapperWidget-Sideload'
}

$packageFolder = Find-PackageFolder
$desktopExe = Find-DesktopExe
$widgetPackage = Get-ChildItem -Path $packageFolder.FullName -Filter *.msix -File | Select-Object -First 1
if (-not $widgetPackage) {
    throw "No .msix package found in $($packageFolder.FullName)"
}

$dependenciesSource = Join-Path $packageFolder.FullName 'Dependencies'
if (-not (Test-Path $dependenciesSource)) {
    throw "Dependencies folder missing in $($packageFolder.FullName)"
}

if (Test-Path $OutputFolder) {
    Remove-Item -LiteralPath $OutputFolder -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputFolder | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutputFolder 'Desktop') | Out-Null

Copy-Item -LiteralPath $widgetPackage.FullName -Destination (Join-Path $OutputFolder $widgetPackage.Name)
Copy-Item -LiteralPath $dependenciesSource -Destination (Join-Path $OutputFolder 'Dependencies') -Recurse
Copy-Item -LiteralPath $desktopExe.FullName -Destination (Join-Path $OutputFolder 'Desktop\Steam Controller Remapper.exe')
Copy-Item -LiteralPath (Join-Path $scriptRoot 'Install-SteamControllerRemapper.ps1') -Destination (Join-Path $OutputFolder 'Install-SteamControllerRemapper.ps1')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $OutputFolder 'README.md')

$certificatePath = Join-Path $OutputFolder 'SteamControllerRemapperWidget.cer'
Export-PublicCertificate -Thumbprint $CertificateThumbprint -DestinationPath $certificatePath

Write-Host "Created widget sideload bundle at:"
Write-Host $OutputFolder
