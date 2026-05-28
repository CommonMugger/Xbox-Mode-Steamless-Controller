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

function Get-SignToolPath {
    $kitsRoot = 'C:\Program Files (x86)\Windows Kits\10\bin'
    $tool = Get-ChildItem -Path $kitsRoot -Recurse -Filter signtool.exe -ErrorAction SilentlyContinue |
        Where-Object { $_.FullName -like '*\x64\signtool.exe' } |
        Sort-Object FullName -Descending |
        Select-Object -First 1
    if (-not $tool) {
        throw 'signtool.exe was not found in the Windows SDK.'
    }
    return $tool.FullName
}

function Ensure-SignedFile([string]$FilePath, [string]$Thumbprint) {
    $signtool = Get-SignToolPath
    $verify = Start-Process -FilePath $signtool -ArgumentList "verify /pa `"$FilePath`"" -Wait -PassThru -NoNewWindow
    if ($verify.ExitCode -eq 0) {
        return
    }

    Write-Host "Signing file $FilePath"
    $sign = Start-Process -FilePath $signtool -ArgumentList "sign /fd SHA256 /sha1 $Thumbprint /s My `"$FilePath`"" -Wait -PassThru -NoNewWindow
    if ($sign.ExitCode -ne 0) {
        throw "Failed to sign file $FilePath"
    }

    $verify = Start-Process -FilePath $signtool -ArgumentList "verify /pa `"$FilePath`"" -Wait -PassThru -NoNewWindow
    if ($verify.ExitCode -ne 0) {
        throw "File signature verification failed for $FilePath"
    }
}

function Find-DesktopOutput {
    $repoRoot = Resolve-Path (Join-Path $scriptRoot '..\..\..')
    $desktopOutputPath = Join-Path $repoRoot 'build\release\Release'
    $desktopExePath = Join-Path $desktopOutputPath 'Steam Controller Remapper.exe'
    $viiperDllPath = Join-Path $desktopOutputPath 'libVIIPER.dll'
    if (-not (Test-Path $desktopExePath)) {
        throw "Desktop executable not found at $desktopExePath. Build the Release target first."
    }
    if (-not (Test-Path $viiperDllPath)) {
        throw "libVIIPER.dll not found at $viiperDllPath. Build the Release target first."
    }
    return Get-Item -LiteralPath $desktopOutputPath
}

function Copy-DesktopRuntime([string]$SourceRoot, [string]$DestinationRoot) {
    $runtimeFiles = @(
        'Steam Controller Remapper.exe',
        'libVIIPER.dll'
    )

    foreach ($name in $runtimeFiles) {
        $sourcePath = Join-Path $SourceRoot $name
        if (-not (Test-Path $sourcePath)) {
            throw "Required desktop runtime file was not found: $sourcePath"
        }

        Copy-Item -LiteralPath $sourcePath -Destination (Join-Path $DestinationRoot $name) -Force
    }
}

function Assert-DesktopRuntimeLayout([string]$DesktopRoot) {
    $expected = @(
        'Steam Controller Remapper.exe',
        'libVIIPER.dll'
    )
    $actual = Get-ChildItem -LiteralPath $DesktopRoot -File | Select-Object -ExpandProperty Name

    $unexpected = $actual | Where-Object { $_ -notin $expected }
    if ($unexpected) {
        throw "Desktop bundle contains unexpected files: $($unexpected -join ', ')"
    }

    foreach ($name in $expected) {
        if ($name -notin $actual) {
            throw "Desktop bundle is missing required runtime file: $name"
        }
    }
}

function Save-UsbIpInstaller([string]$DestinationDir) {
    New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null
    Write-Host "Downloading latest usbip-win2 x64 installer to bundle..."
    $release = Invoke-RestMethod -Headers @{ 'User-Agent' = 'SteamControllerRemapperBuild' } `
        -Uri 'https://api.github.com/repos/vadimgrn/usbip-win2/releases/latest'
    $asset = $release.assets |
        Where-Object { $_.name -match '^USBip-.*-x64\.exe$' } |
        Select-Object -First 1
    if (-not $asset) {
        throw 'Could not find an x64 usbip-win2 installer asset in the latest GitHub release.'
    }
    $destination = Join-Path $DestinationDir $asset.name
    Invoke-WebRequest -Headers @{ 'User-Agent' = 'SteamControllerRemapperBuild' } `
        -Uri $asset.browser_download_url -OutFile $destination
    Write-Host "Bundled usbip-win2 $($release.tag_name): $($asset.name)"
}

$scriptRoot = Split-Path -Parent $PSCommandPath
if ([string]::IsNullOrWhiteSpace($OutputFolder)) {
    $OutputFolder = Join-Path $scriptRoot 'SteamControllerRemapper-Installer'
}
if ([string]::IsNullOrWhiteSpace($ZipPath)) {
    $ZipPath = Join-Path $scriptRoot 'SteamControllerRemapper-Installer.zip'
}

$packageFolder = Find-PackageFolder
$desktopOutput = Find-DesktopOutput
if (-not (Test-Path (Join-Path $packageFolder.FullName 'Add-AppDevPackage.ps1'))) {
    throw "Add-AppDevPackage.ps1 was not found in $($packageFolder.FullName)"
}
$widgetPackage = Get-ChildItem -Path $packageFolder.FullName -Filter *.msix -File | Select-Object -First 1
if (-not $widgetPackage) {
    throw "No .msix package found in $($packageFolder.FullName)"
}
Ensure-SignedFile -FilePath $widgetPackage.FullName -Thumbprint $CertificateThumbprint

if (Test-Path $OutputFolder) {
    Remove-Item -LiteralPath $OutputFolder -Recurse -Force
}
New-Item -ItemType Directory -Path $OutputFolder | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutputFolder 'Desktop') | Out-Null
New-Item -ItemType Directory -Path (Join-Path $OutputFolder 'WidgetPackage') | Out-Null

Copy-Item -Path (Join-Path $packageFolder.FullName '*') -Destination (Join-Path $OutputFolder 'WidgetPackage') -Recurse
Copy-DesktopRuntime -SourceRoot $desktopOutput.FullName -DestinationRoot (Join-Path $OutputFolder 'Desktop')
Save-UsbIpInstaller -DestinationDir (Join-Path $OutputFolder 'usbip')
Copy-Item -LiteralPath (Join-Path $scriptRoot 'Install-SteamControllerRemapper.cmd') -Destination (Join-Path $OutputFolder 'Install-SteamControllerRemapper.cmd')
Copy-Item -LiteralPath (Join-Path $scriptRoot 'Install-SteamControllerRemapper.ps1') -Destination (Join-Path $OutputFolder 'Install-SteamControllerRemapper.ps1')
Copy-Item -LiteralPath (Join-Path $PSScriptRoot 'README.md') -Destination (Join-Path $OutputFolder 'README.md')
Ensure-SignedFile -FilePath (Join-Path $OutputFolder 'Desktop\Steam Controller Remapper.exe') -Thumbprint $CertificateThumbprint
Assert-DesktopRuntimeLayout -DesktopRoot (Join-Path $OutputFolder 'Desktop')

$certificatePath = Join-Path $OutputFolder 'SteamControllerRemapperWidget.cer'
Export-PublicCertificate -Thumbprint $CertificateThumbprint -DestinationPath $certificatePath
Copy-Item -LiteralPath $certificatePath -Destination (Join-Path $OutputFolder 'WidgetPackage\SteamControllerRemapperWidget.cer') -Force

if (Test-Path $ZipPath) {
    Remove-Item -LiteralPath $ZipPath -Force
}
Compress-Archive -Path (Join-Path $OutputFolder '*') -DestinationPath $ZipPath -Force

Write-Host "Created installer bundle at:"
Write-Host $OutputFolder
Write-Host "Created installer archive at:"
Write-Host $ZipPath
