#!/usr/bin/env bash
set -euo pipefail

if [[ -f "$PWD/scripts/setup-arduino.sh" ]]; then
  REPO_ROOT="$PWD"
else
  SCRIPT_DIR="${BASH_SOURCE[0]%/*}"
  [[ "$SCRIPT_DIR" == "${BASH_SOURCE[0]}" ]] && SCRIPT_DIR="."
  REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
fi
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
copy_env_if_set() {
  local source_name="$1"
  local target_name="$2"

  if [[ -z "${!target_name:-}" && -n "${!source_name:-}" ]]; then
    export "$target_name=${!source_name}"
  fi
}

normalize_proxy_environment() {
  copy_env_if_set http_proxy HTTP_PROXY
  copy_env_if_set https_proxy HTTPS_PROXY
  copy_env_if_set all_proxy ALL_PROXY
  copy_env_if_set no_proxy NO_PROXY
  copy_env_if_set HTTP_PROXY http_proxy
  copy_env_if_set HTTPS_PROXY https_proxy
  copy_env_if_set ALL_PROXY all_proxy
  copy_env_if_set NO_PROXY no_proxy
}

print_proxy_environment_summary() {
  local names=(
    "HTTP_PROXY"
    "HTTPS_PROXY"
    "ALL_PROXY"
    "NO_PROXY"
    "http_proxy"
    "https_proxy"
    "all_proxy"
    "no_proxy"
  )
  local name

  echo "Proxy environment:"
  for name in "${names[@]}"; do
    if [[ -n "${!name:-}" ]]; then
      echo "  $name is set"
    else
      echo "  $name is not set"
    fi
  done
}

verify_index_url_with_curl() {
  local url="$1"

  echo "Checking network access with curl: $url"
  curl -fsSI "$url" >/dev/null
}
arduino_cli_proxy_source() {
  local name

  for name in HTTPS_PROXY https_proxy HTTP_PROXY http_proxy ALL_PROXY all_proxy; do
    if [[ -n "${!name:-}" ]]; then
      printf '%s\n' "$name"
      return
    fi
  done
}

arduino_cli_network_proxy() {
  local source_name
  source_name="$(arduino_cli_proxy_source)"

  if [[ -n "$source_name" ]]; then
    printf '%s\n' "${!source_name}"
  fi
}

initialize_arduino_config() {
  if [[ -n "${ARDUINO_CONFIG_FILE:-}" ]]; then
    echo "Using user-provided ARDUINO_CONFIG_FILE=$ARDUINO_CONFIG_FILE"
    return
  fi

  local arduino_home local_dir config_file network_proxy network_config
  arduino_home="$(project_arduino_home)"
  local_dir="$REPO_ROOT/.local"
  config_file="$local_dir/arduino-cli.yaml"
  network_proxy="$(arduino_cli_network_proxy)"
  network_config=""
  if [[ -n "$network_proxy" ]]; then
    network_config="network:
  proxy: $(yaml_single_quote "$network_proxy")"
    echo "Configured Arduino CLI network.proxy from $(arduino_cli_proxy_source)."
  else
    echo "Arduino CLI network.proxy is not configured."
  fi

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

$network_config

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

normalize_proxy_environment
print_proxy_environment_summary

initialize_arduino_config

verify_index_url_with_curl "https://downloads.arduino.cc/libraries/library_index.tar.bz2"
verify_index_url_with_curl "https://downloads.arduino.cc/packages/package_index.tar.bz2"

arduino-cli --log-level trace core update-index
arduino-cli core install arduino:samd

arduino-cli --log-level trace lib update-index
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

if [[ -f "$REPO_ROOT/sketches/Smaeenhouse/sketch.yaml" ]]; then
  bash "$REPO_ROOT/scripts/check-arduino.sh"
fi

arduino-cli version
arduino-cli core list
