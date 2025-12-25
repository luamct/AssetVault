#!/usr/bin/env bash
set -euo pipefail

# Reset AssetVault local data to a first-run state by deleting the database and thumbnails.
# Mirrors the data paths used in Config::get_data_directory().

detect_data_dir() {
  if [[ -n "${TESTING:-}" ]]; then
    # Matches test-time override
    printf "%s\n" "$(pwd)/build/data"
    return
  fi

  local os_name
  os_name="$(uname -s 2>/dev/null || echo unknown)"
  case "$os_name" in
    Darwin)
      if [[ -n "${HOME:-}" ]]; then
        printf "%s\n" "$HOME/Library/Application Support/AssetVault"
        return
      fi
      ;;
    MINGW*|MSYS*|CYGWIN*)
      if [[ -n "${LOCALAPPDATA:-}" ]]; then
        printf "%s\n" "$LOCALAPPDATA/AssetVault"
        return
      fi
      ;;
  esac

  # Fallback aligns with Config::get_data_directory default
  printf "%s\n" "$(pwd)/data"
}

DATA_DIR="$(detect_data_dir)"
DB_PATH="$DATA_DIR/assets.db"
THUMB_DIR="$DATA_DIR/thumbnails"

echo "Resetting AssetVault data in: $DATA_DIR"

if [[ -f "$DB_PATH" ]]; then
  rm -f "$DB_PATH"
  echo "Deleted database: $DB_PATH"
else
  echo "Database not found (already clean): $DB_PATH"
fi

if [[ -d "$THUMB_DIR" ]]; then
  rm -rf "$THUMB_DIR"
  echo "Deleted thumbnails directory: $THUMB_DIR"
else
  echo "Thumbnails directory not found (already clean): $THUMB_DIR"
fi
