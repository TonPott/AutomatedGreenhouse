#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO_ROOT"

project_arduino_home() {
  if [[ -n "${ARDUINO_PROJECT_HOME:-}" ]]; then
    case "$ARDUINO_PROJECT_HOME" in
      /*) printf '%s\n' "$ARDUINO_PROJECT_HOME" ;;
      *) printf '%s\n' "$PWD/$ARDUINO_PROJECT_HOME" ;;
    esac
  else
    printf '%s\n' "$HOME/.cache/arduino-codex/$(basename "$REPO_ROOT")/arduino-cli"
  fi
}

yaml_single_quote() {
  printf "'%s'" "$(printf '%s' "$1" | sed "s/'/''/g")"
}

initialize_arduino_config() {
  if [[ -n "${ARDUINO_CONFIG_FILE:-}" ]]; then
    echo "Using user-provided ARDUINO_CONFIG_FILE=$ARDUINO_CONFIG_FILE"
    return
  fi

  local arduino_home local_dir config_file
  arduino_home="$(project_arduino_home)"
  local_dir="$REPO_ROOT/.local"
  config_file="$local_dir/arduino-cli.yaml"

  mkdir -p "$local_dir" "$arduino_home/data" "$arduino_home/downloads" "$arduino_home/user"

  cat >"$config_file" <<EOF
board_manager:
  additional_urls: []

build_cache:
  path: $(yaml_single_quote "$REPO_ROOT/.arduino-cache")

directories:
  data: $(yaml_single_quote "$arduino_home/data")
  downloads: $(yaml_single_quote "$arduino_home/downloads")
  user: $(yaml_single_quote "$arduino_home/user")

library:
  enable_unsafe_install: false

logging:
  level: info
  format: text
EOF

  export ARDUINO_CONFIG_FILE="$config_file"
  echo "Using shared Arduino CLI home: $arduino_home"
  echo "Generated Arduino CLI config: $config_file"
}

mkdir -p "$HOME/.local/bin"

if ! command -v arduino-cli >/dev/null 2>&1; then
  curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh \
    | BINDIR="$HOME/.local/bin" sh
fi

export PATH="$HOME/.local/bin:$PATH"

initialize_arduino_config

arduino-cli core update-index
arduino-cli core install arduino:samd

arduino-cli lib update-index
arduino-cli lib install Arduino_SpiNINA@0.0.2
arduino-cli lib install WiFiNINA@2.0.1
arduino-cli lib install "Sensirion Core@0.7.3"
arduino-cli lib install "Sensirion I2C SHT3x@1.0.1"
arduino-cli lib install "Adafruit BusIO@1.17.4"
arduino-cli lib install RTClib@2.1.4
arduino-cli lib install PubSubClient@2.8.0
arduino-cli lib install home-assistant-integration@2.1.0
arduino-cli lib install AD5263@0.1.4
arduino-cli lib install JC_EEPROM@1.0.10
arduino-cli lib install Streaming@6.3.0

if [[ -f "$REPO_ROOT/sketches/Smaeenhouse/sketch.yaml" ]]; then
  bash "$REPO_ROOT/scripts/check-arduino.sh"
fi

arduino-cli version
arduino-cli core list
