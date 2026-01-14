#!/usr/bin/env bash
set -euo pipefail

DIR="/home/tung/clean_file"
SLEEP_BEFORE_DELETE=5
CHECK_INTERVAL=1

if [[ ! -d "$DIR" ]]; then
  echo "ERROR: Directory not found: $DIR" >&2
  exit 1
fi

while true; do
  # Kiểm tra có file thường nào trong thư mục không
  if find "$DIR" -mindepth 1 -maxdepth 1 -type f -print -quit | grep -q .; then
    echo "$(date -Is) Found files in $DIR. Deleting in ${SLEEP_BEFORE_DELETE}s..."
    sleep "$SLEEP_BEFORE_DELETE"

    # Xóa các file thường (không động tới thư mục)
    find "$DIR" -mindepth 1 -maxdepth 1 -type f -exec rm -f -- {} +
    echo "$(date -Is) Deleted files in $DIR."
  else
    sleep "$CHECK_INTERVAL"
  fi
done
