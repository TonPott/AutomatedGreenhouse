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
  if ! curl -fsSI "$url" >/dev/null; then
    echo "Warning: curl could not verify $url; continuing so Arduino CLI can use any cached indexes." >&2
  fi
}

install_profile_libraries() {
  local sketch_yaml="$1"

  if [[ ! -f "$sketch_yaml" ]]; then
    echo "Sketch profile not found: $sketch_yaml" >&2
    return 1
  fi

  python3 - "$sketch_yaml" <<'PY' | while IFS= read -r library; do
import re
import sys

sketch_yaml = sys.argv[1]
in_libraries = False
base_indent = None
with open(sketch_yaml, encoding="utf-8") as handle:
    for raw_line in handle:
        line = raw_line.rstrip("\n")
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if stripped == "libraries:":
            in_libraries = True
            base_indent = len(line) - len(line.lstrip())
            continue
        if in_libraries:
            indent = len(line) - len(line.lstrip())
            if indent <= base_indent and not stripped.startswith("-"):
                break
            match = re.match(r"-\s+(.+?)\s+\(([^()]+)\)\s*$", stripped)
            if match:
                print(f"{match.group(1)}@{match.group(2)}")
PY
    if [[ -n "$library" ]]; then
      arduino-cli lib install "$library"
    fi
  done
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

if ! arduino-cli --log-level trace core update-index; then
  echo "Warning: Arduino core index update failed; continuing with cached core index if available." >&2
fi
arduino-cli core install arduino:samd

if ! arduino-cli --log-level trace lib update-index; then
  echo "Warning: Arduino library index update failed; continuing with cached library index if available." >&2
fi
install_profile_libraries "$REPO_ROOT/sketches/alpha/Smaeenhouse/sketch.yaml"

if [[ -f "$REPO_ROOT/sketches/alpha/Smaeenhouse/sketch.yaml" ]]; then
  bash "$REPO_ROOT/scripts/check-arduino.sh"
fi

arduino-cli version
arduino-cli core list
