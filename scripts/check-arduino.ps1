$ErrorActionPreference = "Stop"

$RepoRoot = Split-Path -Parent $PSScriptRoot
Set-Location $RepoRoot

$ToolchainMissingMessage = "Arduino toolchain is not prepared. Run scripts/setup-arduino once, then retry the compile check."

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

function Convert-ToYamlSingleQuotedPath([string] $Path) {
    return "'" + $Path.Replace("\", "/").Replace("'", "''") + "'"
}

function Initialize-ArduinoConfig {
    if ($env:ARDUINO_CONFIG_FILE) {
        Write-Host "Using user-provided ARDUINO_CONFIG_FILE=$env:ARDUINO_CONFIG_FILE"
        return
    }

    $ArduinoHome = Get-ProjectArduinoHome
    $LocalDir = Join-Path $RepoRoot ".local"
    $ConfigFile = Join-Path $LocalDir "arduino-cli.yaml"

    New-Item -ItemType Directory -Force -Path $LocalDir | Out-Null

    $DataPath = Convert-ToYamlSingleQuotedPath (Join-Path $ArduinoHome "data")
    $DownloadsPath = Convert-ToYamlSingleQuotedPath (Join-Path $ArduinoHome "downloads")
    $UserPath = Convert-ToYamlSingleQuotedPath (Join-Path $ArduinoHome "user")
    $BuildCachePath = Convert-ToYamlSingleQuotedPath (Join-Path $RepoRoot ".arduino-cache")

@"
board_manager:
  additional_urls: []

build_cache:
  path: $BuildCachePath

directories:
  data: $DataPath
  downloads: $DownloadsPath
  user: $UserPath

library:
  enable_unsafe_install: false

logging:
  level: info
  format: text
"@ | Set-Content -Path $ConfigFile -Encoding utf8

    $env:ARDUINO_CONFIG_FILE = $ConfigFile
    Write-Host "Using shared Arduino CLI home: $ArduinoHome"
    Write-Host "Generated Arduino CLI config: $ConfigFile"
}

function Assert-ArduinoToolchainPrepared([string] $RequiredCore, [string[]] $RequiredLibraries) {
    if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
        throw $ToolchainMissingMessage
    }

    $CoreList = arduino-cli core list 2>$null
    if ($LASTEXITCODE -ne 0 -or -not ($CoreList -match "(?m)^$([regex]::Escape($RequiredCore))\s")) {
        throw $ToolchainMissingMessage
    }

    if ($RequiredLibraries.Count -eq 0) {
        return
    }

    $InstalledLibrariesJson = arduino-cli lib list --json 2>$null
    if ($LASTEXITCODE -ne 0) {
        throw $ToolchainMissingMessage
    }

    $InstalledLibraries = ($InstalledLibrariesJson | ConvertFrom-Json).installed_libraries
    foreach ($Library in $RequiredLibraries) {
        $MatchingLibrary = $InstalledLibraries | Where-Object {
            $_.library.name -eq $Library
        } | Select-Object -First 1

        if (-not $MatchingLibrary) {
            throw $ToolchainMissingMessage
        }
    }
}

function Get-SafeSketchName([string] $SketchPath) {
    $FullSketchPath = [System.IO.Path]::GetFullPath($SketchPath)
    $FullRepoRoot = [System.IO.Path]::GetFullPath($RepoRoot).TrimEnd([System.IO.Path]::DirectorySeparatorChar, [System.IO.Path]::AltDirectorySeparatorChar)
    $SketchKey = $FullSketchPath

    if ($FullSketchPath.StartsWith($FullRepoRoot + [System.IO.Path]::DirectorySeparatorChar, [System.StringComparison]::OrdinalIgnoreCase)) {
        $SketchKey = $FullSketchPath.Substring($FullRepoRoot.Length + 1)
    }

    $SafeName = $SketchKey -replace '^[A-Za-z]:', ''
    $SafeName = $SafeName -replace '[\\/:*?"<>|\s]+', '_'
    $SafeName = $SafeName.Trim("_")

    if (-not $SafeName) {
        return "sketch"
    }

    return $SafeName
}

function New-TemporaryCredentialsIfNeeded([string] $SketchPath) {
    $CredentialsPath = Join-Path $SketchPath "Credentials.h"
    $ExamplePath = Join-Path $SketchPath "Credentials.example.h"
    $ProductionExamplePath = Join-Path $RepoRoot "sketches/Smaeenhouse/Credentials.example.h"

    if (Test-Path -LiteralPath $CredentialsPath) {
        return $null
    }

    if (-not (Test-Path -LiteralPath $ExamplePath)) {
        $ExamplePath = $ProductionExamplePath
    }

    if (-not (Test-Path -LiteralPath $ExamplePath)) {
        return $null
    }

    Copy-Item -LiteralPath $ExamplePath -Destination $CredentialsPath
    Write-Host "Generated temporary Credentials.h from Credentials.example.h for compile check."
    return $CredentialsPath
}

Initialize-ArduinoConfig

$DefaultSketch = "sketches/Smaeenhouse"
$Sketch = if ($env:SKETCH) { $env:SKETCH } else { $DefaultSketch }
$Fqbn = if ($env:FQBN) { $env:FQBN } else { "arduino:samd:nano_33_iot" }
$Profile = if ($env:PROFILE) { $env:PROFILE } else { "nano33iot" }

$SketchPath = if ([System.IO.Path]::IsPathRooted($Sketch)) {
    $Sketch
} else {
    Join-Path $RepoRoot $Sketch
}

$RequiredCore = "arduino:samd"
$RequiredLibraries = @(
    "Arduino_SpiNINA",
    "WiFiNINA",
    "Sensirion Core",
    "Sensirion I2C SHT3x",
    "Adafruit BusIO",
    "Adafruit Unified Sensor",
    "Adafruit TSL2591 Library",
    "RTClib",
    "PubSubClient",
    "home-assistant-integration",
    "ArduinoOTA",
    "AD5263",
    "JC_EEPROM",
    "Streaming"
)

if ($Profile -eq "mega2560") {
    $RequiredCore = "arduino:avr"
    $RequiredLibraries = @()
}

if (-not (Test-Path (Join-Path $SketchPath "sketch.yaml"))) {
    $FqbnParts = $Fqbn -split ":"
    $RequiredCore = $FqbnParts[0..1] -join ":"
    $RequiredLibraries = @()
}

Assert-ArduinoToolchainPrepared $RequiredCore $RequiredLibraries

$SafeSketchName = Get-SafeSketchName $SketchPath
$BuildPath = Join-Path (Join-Path $RepoRoot ".build") $SafeSketchName
$BuildCachePath = Join-Path $RepoRoot ".arduino-cache"

New-Item -ItemType Directory -Force -Path $BuildPath | Out-Null
New-Item -ItemType Directory -Force -Path $BuildCachePath | Out-Null

$TemporaryCredentialsPath = New-TemporaryCredentialsIfNeeded $SketchPath
try {
    arduino-cli compile --fqbn $Fqbn --build-path $BuildPath $SketchPath
} finally {
    if ($TemporaryCredentialsPath -and (Test-Path -LiteralPath $TemporaryCredentialsPath)) {
        Remove-Item -LiteralPath $TemporaryCredentialsPath -Force
        Write-Host "Removed temporary Credentials.h."
    }
}
