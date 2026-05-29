#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd -P)"
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

resolve_removal_target() {
  local target parent base resolved_parent
  target="$1"

  if [[ -z "$target" || "$target" == "/" ]]; then
    echo "Refusing to remove unsafe path: ${target:-<empty>}" >&2
    exit 1
  fi

  parent="$(dirname "$target")"
  base="$(basename "$target")"

  if [[ ! -d "$parent" ]]; then
    printf '%s\n' "$target"
    return
  fi

  resolved_parent="$(cd "$parent" && pwd -P)"
  printf '%s/%s\n' "$resolved_parent" "$base"
}

assert_safe_removal_target() {
  local target
  target="$1"

  case "$target" in
    ""|"/"|"$HOME"|"$REPO_ROOT"|"$PWD")
      echo "Refusing to remove unsafe path: ${target:-<empty>}" >&2
      exit 1
      ;;
  esac
}

ARDUINO_HOME="$(resolve_removal_target "$(project_arduino_home)")"
assert_safe_removal_target "$ARDUINO_HOME"

echo "Shared Arduino CLI toolchain path:"
echo "$ARDUINO_HOME"
echo "Deleting this directory means the next setup will need to download cores and libraries again."
echo "This script is for manual project-end cleanup only. Do not use it as a Codex automatic cleanup script."

if [[ ! -e "$ARDUINO_HOME" ]]; then
  echo "Nothing to remove; directory does not exist."
  exit 0
fi

printf 'Type DELETE to remove the shared Arduino CLI toolchain: '
read -r confirmation

if [[ "$confirmation" != "DELETE" ]]; then
  echo "Cleanup cancelled."
  exit 0
fi

rm -rf "$ARDUINO_HOME"
echo "Removed shared Arduino CLI toolchain."
