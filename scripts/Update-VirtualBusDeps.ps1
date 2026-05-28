param(
    [string]$VersionFile = "cmake/VirtualBusVersions.cmake",
    [string]$ViiperDir = "third_party/libVIIPER",
    [switch]$Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-VersionValue([string]$Content, [string]$Name) {
    $match = [regex]::Match($Content, "set\($Name\s+`"(?<version>[^`"]+)`"\)")
    if (-not $match.Success) {
        throw "Could not read $Name from $VersionFile"
    }
    return $match.Groups["version"].Value
}

function Get-VersionState([string]$Path) {
    $content = Get-Content -LiteralPath $Path -Raw
    return [pscustomobject]@{
        Content = $content
        ViiperVersion = Get-VersionValue -Content $content -Name "SCR_VIIPER_VERSION"
        UsbIpVersion = Get-VersionValue -Content $content -Name "SCR_USBIP_WIN2_VERSION"
    }
}

function New-GitHubHeaders([string]$UserAgent) {
    return @{
        "Accept" = "application/vnd.github+json"
        "User-Agent" = $UserAgent
        "X-GitHub-Api-Version" = "2022-11-28"
    }
}

function Normalize-VersionTag([string]$Tag) {
    $match = [regex]::Match($Tag, '(?<version>\d+(?:\.\d+){1,3})')
    if (-not $match.Success) {
        throw "Unsupported version tag format: $Tag"
    }
    return $match.Groups["version"].Value
}

function Get-LatestViiperRelease() {
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/Alia5/VIIPER/releases/latest" `
        -Headers (New-GitHubHeaders -UserAgent "SteamControllerRemapper-VirtualBus-Updater")
    $asset = $release.assets | Where-Object { $_.name -eq 'viiper-libVIIPER-windows-amd64.zip' } | Select-Object -First 1
    if (-not $asset) {
        throw "Could not find viiper-libVIIPER-windows-amd64.zip in the latest VIIPER release."
    }
    return [pscustomobject]@{
        Version = Normalize-VersionTag -Tag ([string]$release.tag_name)
        Url = [string]$asset.browser_download_url
        Name = [string]$asset.name
    }
}

function Get-LatestUsbIpRelease() {
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/vadimgrn/usbip-win2/releases/latest" `
        -Headers (New-GitHubHeaders -UserAgent "SteamControllerRemapper-VirtualBus-Updater")
    $asset = $release.assets | Where-Object { $_.name -match '^USBip-.*-x64\.exe$' } | Select-Object -First 1
    if (-not $asset) {
        throw "Could not find an x64 usbip-win2 installer in the latest release."
    }
    return [pscustomobject]@{
        Version = Normalize-VersionTag -Tag ([string]$release.tag_name)
        Url = [string]$asset.browser_download_url
        Name = [string]$asset.name
    }
}

function Compare-Version([string]$Left, [string]$Right) {
    return ([Version]$Left).CompareTo([Version]$Right)
}

function Get-ChangeKind([string]$Current, [string]$Latest) {
    $currentParts = $Current.Split('.')
    $latestParts = $Latest.Split('.')
    $count = [Math]::Max($currentParts.Count, $latestParts.Count)
    for ($i = $currentParts.Count; $i -lt $count; $i++) { $currentParts += '0' }
    for ($i = $latestParts.Count; $i -lt $count; $i++) { $latestParts += '0' }

    if ($currentParts[0] -ne $latestParts[0]) { return "major" }
    if ($currentParts.Count -gt 1 -and $currentParts[1] -ne $latestParts[1]) { return "minor" }
    if ($count -gt 2 -and $currentParts[2] -ne $latestParts[2]) { return "patch" }
    if ($count -gt 3 -and $currentParts[3] -ne $latestParts[3]) { return "patch" }
    return "none"
}

function Get-OverallChangeKind([string[]]$Kinds) {
    if ($Kinds -contains "major") { return "major" }
    if ($Kinds -contains "minor") { return "minor" }
    if ($Kinds -contains "patch") { return "patch" }
    return "none"
}

function Set-GitHubOutput([string]$Name, [string]$Value) {
    if (-not $env:GITHUB_OUTPUT) {
        return
    }
    "$Name=$Value" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
}

function Update-VersionFile([string]$Path, [string]$Content, [string]$ViiperVersion, [string]$UsbIpVersion) {
    $updated = [regex]::Replace($Content, 'set\(SCR_VIIPER_VERSION\s+"[^"]+"\)', "set(SCR_VIIPER_VERSION `"$ViiperVersion`")")
    $updated = [regex]::Replace($updated, 'set\(SCR_USBIP_WIN2_VERSION\s+"[^"]+"\)', "set(SCR_USBIP_WIN2_VERSION `"$UsbIpVersion`")")
    Set-Content -LiteralPath $Path -Value $updated -Encoding ascii
}

function Update-VendoredViiper([string]$DestinationDir, [pscustomobject]$Release) {
    $downloadDir = Join-Path ([System.IO.Path]::GetTempPath()) "SteamControllerRemapper-VIIPER"
    $zipPath = Join-Path $downloadDir $Release.Name
    $extractDir = Join-Path $downloadDir "extract"

    Remove-Item -LiteralPath $downloadDir -Recurse -Force -ErrorAction SilentlyContinue
    New-Item -ItemType Directory -Path $downloadDir -Force | Out-Null
    Invoke-WebRequest -Uri $Release.Url -Headers (New-GitHubHeaders -UserAgent "SteamControllerRemapper-VirtualBus-Updater") -OutFile $zipPath
    Expand-Archive -LiteralPath $zipPath -DestinationPath $extractDir -Force

    $dllPath = Join-Path $extractDir "libVIIPER.dll"
    $headerPath = Join-Path $extractDir "libVIIPER.h"
    if (-not (Test-Path $dllPath) -or -not (Test-Path $headerPath)) {
        throw "VIIPER release archive did not contain libVIIPER.dll and libVIIPER.h"
    }

    New-Item -ItemType Directory -Path $DestinationDir -Force | Out-Null
    Copy-Item -LiteralPath $dllPath -Destination (Join-Path $DestinationDir "libVIIPER.dll") -Force
    Copy-Item -LiteralPath $headerPath -Destination (Join-Path $DestinationDir "libVIIPER.h") -Force
}

$state = Get-VersionState -Path $VersionFile
$latestViiper = Get-LatestViiperRelease
$latestUsbIp = Get-LatestUsbIpRelease

$viiperChanged = (Compare-Version -Left $state.ViiperVersion -Right $latestViiper.Version) -lt 0
$usbipChanged = (Compare-Version -Left $state.UsbIpVersion -Right $latestUsbIp.Version) -lt 0
$changeKinds = @()
if ($viiperChanged) { $changeKinds += (Get-ChangeKind -Current $state.ViiperVersion -Latest $latestViiper.Version) }
if ($usbipChanged) { $changeKinds += (Get-ChangeKind -Current $state.UsbIpVersion -Latest $latestUsbIp.Version) }
$overallChangeKind = Get-OverallChangeKind -Kinds $changeKinds
$changed = $viiperChanged -or $usbipChanged

Write-Host "Pinned VIIPER version : $($state.ViiperVersion)"
Write-Host "Latest VIIPER version : $($latestViiper.Version)"
Write-Host "Pinned USBIP version  : $($state.UsbIpVersion)"
Write-Host "Latest USBIP version  : $($latestUsbIp.Version)"
Write-Host "Change kind           : $overallChangeKind"

Set-GitHubOutput -Name "current_viiper_version" -Value $state.ViiperVersion
Set-GitHubOutput -Name "latest_viiper_version" -Value $latestViiper.Version
Set-GitHubOutput -Name "current_usbip_version" -Value $state.UsbIpVersion
Set-GitHubOutput -Name "latest_usbip_version" -Value $latestUsbIp.Version
Set-GitHubOutput -Name "changed" -Value ($(if ($changed) { "true" } else { "false" }))
Set-GitHubOutput -Name "change_kind" -Value $overallChangeKind
Set-GitHubOutput -Name "viiper_changed" -Value ($(if ($viiperChanged) { "true" } else { "false" }))
Set-GitHubOutput -Name "usbip_changed" -Value ($(if ($usbipChanged) { "true" } else { "false" }))

if (-not $changed -or -not $Apply) {
    return
}

if ($viiperChanged) {
    Update-VendoredViiper -DestinationDir $ViiperDir -Release $latestViiper
    Write-Host "Updated vendored libVIIPER to $($latestViiper.Version)"
}

Update-VersionFile -Path $VersionFile -Content $state.Content -ViiperVersion $latestViiper.Version -UsbIpVersion $latestUsbIp.Version
Write-Host "Updated $VersionFile"
