param(
    [switch] $Force
)

$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

function Get-ProjectArduinoHome {
    if ($env:ARDUINO_PROJECT_HOME) {
        return [System.IO.Path]::GetFullPath($env:ARDUINO_PROJECT_HOME)
    }

    $LocalAppData = if ($env:LOCALAPPDATA) {
        $env:LOCALAPPDATA
    } else {
        [Environment]::GetFolderPath("LocalApplicationData")
    }

    if (-not $LocalAppData) {
        $LocalAppData = Join-Path $env:USERPROFILE "AppData\Local"
    }

    $RepoName = Split-Path -Leaf $RepoRoot
    return (Join-Path (Join-Path $LocalAppData "ArduinoCodex") (Join-Path $RepoName "arduino-cli"))
}

function Assert-SafeRemovalTarget([string] $Target) {
    if (-not $Target) {
        throw "Refusing to remove an empty path."
    }

    $TargetFullPath = [System.IO.Path]::GetFullPath($Target).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    $RepoFullPath = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    $HomeFullPath = [System.IO.Path]::GetFullPath($env:USERPROFILE).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    $RootFullPath = [System.IO.Path]::GetPathRoot($TargetFullPath).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)

    $UnsafePaths = @($RepoFullPath, $HomeFullPath, $RootFullPath)

    if ($env:LOCALAPPDATA) {
        $UnsafePaths += [System.IO.Path]::GetFullPath($env:LOCALAPPDATA).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    }

    foreach ($UnsafePath in $UnsafePaths) {
        if ($UnsafePath -and [string]::Equals($TargetFullPath, $UnsafePath, [System.StringComparison]::OrdinalIgnoreCase)) {
            throw "Refusing to remove unsafe path: $TargetFullPath"
        }
    }

    return $TargetFullPath
}

$ArduinoHome = Assert-SafeRemovalTarget (Get-ProjectArduinoHome)

Write-Host "Shared Arduino CLI toolchain path:"
Write-Host $ArduinoHome
Write-Host "Deleting this directory means the next setup will need to download cores and libraries again."
Write-Host "This script is for manual project-end cleanup only. Do not use it as a Codex automatic cleanup script."

if (-not (Test-Path -LiteralPath $ArduinoHome)) {
    Write-Host "Nothing to remove; directory does not exist."
    exit 0
}

if (-not $Force) {
    $Confirmation = Read-Host "Type DELETE to remove the shared Arduino CLI toolchain"
    if ($Confirmation -ne "DELETE") {
        Write-Host "Cleanup cancelled."
        exit 0
    }
}

Remove-Item -LiteralPath $ArduinoHome -Recurse -Force
Write-Host "Removed shared Arduino CLI toolchain."
