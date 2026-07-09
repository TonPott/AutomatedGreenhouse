#!/usr/bin/env bash
set -euo pipefail

if [[ -f "$PWD/scripts/cleanup-worktree.sh" ]]; then
  REPO_ROOT="$PWD"
else
  SCRIPT_DIR="${BASH_SOURCE[0]%/*}"
  [[ "$SCRIPT_DIR" == "${BASH_SOURCE[0]}" ]] && SCRIPT_DIR="."
  REPO_ROOT="$(cd "$SCRIPT_DIR/.." && pwd -P)"
fi

local_directories=(
  ".build"
  ".arduino-cache"
  ".local"
  ".arduino"
)

for directory in "${local_directories[@]}"; do
  target="$REPO_ROOT/$directory"

  case "$target" in
    "$REPO_ROOT"/*) ;;
    *)
      echo "Refusing to remove path outside repository: $target" >&2
      exit 1
      ;;
  esac

  if [[ -e "$target" ]]; then
    rm -rf "$target"
    echo "Removed $directory"
  else
    echo "Skipped $directory (not present)"
  fi
done

echo "Worktree cleanup complete. Shared Arduino CLI toolchain was not touched."
