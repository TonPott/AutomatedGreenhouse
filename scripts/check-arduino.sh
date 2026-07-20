#!/usr/bin/env bash
set -euo pipefail

if [[ -f "$PWD/scripts/check-arduino.sh" ]]; then
  REPO_ROOT="$PWD"
else
  SCRIPT_DIR="${BASH_SOURCE[0]%/*}"
  [[ "$SCRIPT_DIR" == "${BASH_SOURCE[0]}" ]] && SCRIPT_DIR="."
  REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
fi
cd "$REPO_ROOT"

TOOLCHAIN_MISSING_MESSAGE="Arduino toolchain is not prepared. Run scripts/setup-arduino once, then retry the compile check."

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

  mkdir -p "$local_dir"

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

assert_arduino_toolchain_prepared() {
  local required_core="$1"
  shift
  local required_libraries=("$@")

  if ! command -v arduino-cli >/dev/null 2>&1; then
    echo "$TOOLCHAIN_MISSING_MESSAGE" >&2
    exit 1
  fi

  if ! arduino-cli core list 2>/dev/null | grep -Eq "^${required_core}[[:space:]]"; then
    echo "$TOOLCHAIN_MISSING_MESSAGE" >&2
    exit 1
  fi

  if [[ "${#required_libraries[@]}" -eq 0 ]]; then
    return
  fi

  local installed_libraries_json library
  installed_libraries_json="$(arduino-cli lib list --json 2>/dev/null)" || {
    echo "$TOOLCHAIN_MISSING_MESSAGE" >&2
    exit 1
  }

  for library in "${required_libraries[@]}"; do
    if ! printf '%s\n' "$installed_libraries_json" | awk -v expected_name="$library" '
      /"name":/ {
        current_name = $0
        sub(/^[[:space:]]*"name":[[:space:]]*"/, "", current_name)
        sub(/",[[:space:]]*$/, "", current_name)
        if (current_name == expected_name) {
          found = 1
        }
      }
      END { exit found ? 0 : 1 }
    '; then
      echo "$TOOLCHAIN_MISSING_MESSAGE" >&2
      exit 1
    fi
  done
}

safe_sketch_name() {
  local sketch_path full_repo full_sketch sketch_key safe_name
  sketch_path="$1"
  full_repo="$(cd "$REPO_ROOT" && pwd -P)"
  full_sketch="$(cd "$(dirname "$sketch_path")" && pwd -P)/$(basename "$sketch_path")"
  sketch_key="$full_sketch"

  case "$full_sketch" in
    "$full_repo"/*) sketch_key="${full_sketch#"$full_repo/"}" ;;
  esac

  safe_name="$(printf '%s' "$sketch_key" | sed -E 's#^[A-Za-z]:##; s#[/\\:*?"<>|[:space:]]+#_#g; s#^_+##; s#_+$##')"
  if [[ -z "$safe_name" ]]; then
    safe_name="sketch"
  fi

  printf '%s\n' "$safe_name"
}

temporary_credentials_if_needed() {
  local sketch_path credentials_path example_path
  sketch_path="$1"
  credentials_path="$sketch_path/Credentials.h"
  example_path="$sketch_path/Credentials.example.h"
  production_example_path="$REPO_ROOT/sketches/alpha/Smaeenhouse/Credentials.example.h"

  if [[ -f "$credentials_path" ]]; then
    return
  fi

  if [[ ! -f "$example_path" ]]; then
    example_path="$production_example_path"
  fi

  if [[ ! -f "$example_path" ]]; then
    return
  fi

  cp "$example_path" "$credentials_path"
  echo "Generated temporary Credentials.h from Credentials.example.h for compile check."
  printf '%s\n' "$credentials_path"
}

initialize_arduino_config

DEFAULT_SKETCH="sketches/alpha/Smaeenhouse"
FQBN="${FQBN:-arduino:samd:nano_33_iot}"
SKETCH="${SKETCH:-$DEFAULT_SKETCH}"
PROFILE="${PROFILE:-nano33iot}"

case "$SKETCH" in
  /*) SKETCH_PATH="$SKETCH" ;;
  *) SKETCH_PATH="$REPO_ROOT/$SKETCH" ;;
esac

REQUIRED_CORE="arduino:samd"
REQUIRED_LIBRARIES=(
  "Arduino_SpiNINA"
  "WiFiNINA"
  "Sensirion Core"
  "Sensirion I2C SHT3x"
  "Adafruit BusIO"
  "Adafruit Unified Sensor"
  "Adafruit TSL2591 Library"
  "RTClib"
  "PubSubClient"
  "home-assistant-integration"
  "ArduinoOTA"
  "AD5263"
  "JC_EEPROM"
  "Streaming"
)

if [[ "$PROFILE" == "mega2560" ]]; then
  REQUIRED_CORE="arduino:avr"
  REQUIRED_LIBRARIES=()
fi

if [[ ! -f "$SKETCH_PATH/sketch.yaml" ]]; then
  IFS=':' read -r FQBN_VENDOR FQBN_ARCH _ <<<"$FQBN"
  REQUIRED_CORE="$FQBN_VENDOR:$FQBN_ARCH"
  REQUIRED_LIBRARIES=()
fi

assert_arduino_toolchain_prepared "$REQUIRED_CORE" "${REQUIRED_LIBRARIES[@]}"

SAFE_SKETCH_NAME="$(safe_sketch_name "$SKETCH_PATH")"
BUILD_PATH="$REPO_ROOT/.build/$SAFE_SKETCH_NAME"
BUILD_CACHE_PATH="$REPO_ROOT/.arduino-cache"

mkdir -p "$BUILD_PATH" "$BUILD_CACHE_PATH"

TEMPORARY_CREDENTIALS_PATH="$(temporary_credentials_if_needed "$SKETCH_PATH" || true)"
cleanup_temporary_credentials() {
  if [[ -n "${TEMPORARY_CREDENTIALS_PATH:-}" && -f "$TEMPORARY_CREDENTIALS_PATH" ]]; then
    rm -f "$TEMPORARY_CREDENTIALS_PATH"
    echo "Removed temporary Credentials.h."
  fi
}
trap cleanup_temporary_credentials EXIT

arduino-cli compile --fqbn "$FQBN" --build-path "$BUILD_PATH" "$SKETCH_PATH"
