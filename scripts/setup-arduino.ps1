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
    New-Item -ItemType Directory -Force -Path (Join-Path $ArduinoHome "data") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $ArduinoHome "downloads") | Out-Null
    New-Item -ItemType Directory -Force -Path (Join-Path $ArduinoHome "user") | Out-Null

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

if (-not (Get-Command arduino-cli -ErrorAction SilentlyContinue)) {
    throw "arduino-cli was not found. Install Arduino CLI and ensure it is available in PATH."
}

Initialize-ArduinoConfig

arduino-cli core update-index
arduino-cli core install arduino:samd

arduino-cli lib update-index
arduino-cli lib install Arduino_SpiNINA@0.0.2
arduino-cli lib install WiFiNINA@2.0.1
arduino-cli lib install "Sensirion Core@0.7.3"
arduino-cli lib install "Sensirion I2C SHT3x@1.0.1"
arduino-cli lib install "Adafruit BusIO@1.17.4"
arduino-cli lib install "Adafruit Unified Sensor@1.1.15"
arduino-cli lib install "Adafruit TSL2591 Library@1.4.5"
arduino-cli lib install RTClib@2.1.4
arduino-cli lib install PubSubClient@2.8.0
arduino-cli lib install home-assistant-integration@2.1.0
arduino-cli lib install ArduinoOTA@1.1.1
arduino-cli lib install AD5263@0.1.4
arduino-cli lib install JC_EEPROM@1.0.10
arduino-cli lib install Streaming@6.3.0

if (Test-Path (Join-Path $RepoRoot "sketches/alpha/Smaeenhouse/sketch.yaml")) {
    & (Join-Path $PSScriptRoot "check-arduino.ps1")
}

arduino-cli version
arduino-cli core list
