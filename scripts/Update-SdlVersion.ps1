param(
    [string]$VersionFile = "cmake/SDLVersion.cmake",
    [switch]$Apply
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-PinnedVersion([string]$Path) {
    $content = Get-Content -LiteralPath $Path -Raw
    $match = [regex]::Match($content, 'set\(SCR_SDL_VERSION\s+"(?<version>\d+\.\d+\.\d+)"\)')
    if (-not $match.Success) {
        throw "Could not read SCR_SDL_VERSION from $Path"
    }
    return $match.Groups["version"].Value
}

function Get-LatestSdlVersion() {
    $headers = @{
        "Accept" = "application/vnd.github+json"
        "User-Agent" = "SteamControllerRemapper-SDL-Updater"
        "X-GitHub-Api-Version" = "2022-11-28"
    }
    $release = Invoke-RestMethod -Uri "https://api.github.com/repos/libsdl-org/SDL/releases/latest" -Headers $headers
    $tag = [string]$release.tag_name
    if ($tag -match '^release-(?<version>\d+\.\d+\.\d+)$') {
        return $matches["version"]
    }
    if ($tag -match '^v?(?<version>\d+\.\d+\.\d+)$') {
        return $matches["version"]
    }
    throw "Unsupported SDL tag format: $tag"
}

function Compare-Version([string]$Left, [string]$Right) {
    $leftVersion = [Version]$Left
    $rightVersion = [Version]$Right
    return $leftVersion.CompareTo($rightVersion)
}

function Get-ChangeKind([string]$Current, [string]$Latest) {
    $currentParts = $Current.Split('.')
    $latestParts = $Latest.Split('.')
    if ($currentParts[0] -ne $latestParts[0]) { return "major" }
    if ($currentParts[1] -ne $latestParts[1]) { return "minor" }
    if ($currentParts[2] -ne $latestParts[2]) { return "patch" }
    return "none"
}

function Set-GitHubOutput([string]$Name, [string]$Value) {
    if (-not $env:GITHUB_OUTPUT) {
        return
    }
    "$Name=$Value" | Out-File -FilePath $env:GITHUB_OUTPUT -Encoding utf8 -Append
}

$currentVersion = Get-PinnedVersion -Path $VersionFile
$latestVersion = Get-LatestSdlVersion
$comparison = Compare-Version -Left $currentVersion -Right $latestVersion
$changeKind = if ($comparison -lt 0) { Get-ChangeKind -Current $currentVersion -Latest $latestVersion } else { "none" }
$changed = $comparison -lt 0

Write-Host "Pinned SDL version : $currentVersion"
Write-Host "Latest SDL version : $latestVersion"
Write-Host "Change kind        : $changeKind"

Set-GitHubOutput -Name "current_version" -Value $currentVersion
Set-GitHubOutput -Name "latest_version" -Value $latestVersion
Set-GitHubOutput -Name "change_kind" -Value $changeKind
Set-GitHubOutput -Name "changed" -Value ($(if ($changed) { "true" } else { "false" }))

if (-not $changed) {
    return
}

if (-not $Apply) {
    return
}

$content = Get-Content -LiteralPath $VersionFile -Raw
$updated = [regex]::Replace(
    $content,
    'set\(SCR_SDL_VERSION\s+"(?<version>\d+\.\d+\.\d+)"\)',
    "set(SCR_SDL_VERSION `"$latestVersion`")"
)
Set-Content -LiteralPath $VersionFile -Value $updated -Encoding ascii
Write-Host "Updated $VersionFile to SDL $latestVersion"
