#!/usr/bin/env bash
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

if command -v fd >/dev/null 2>&1; then
  FD=fd
elif command -v fdfind >/dev/null 2>&1; then
  FD=fdfind
else
  echo "fd/fdfind is required" >&2
  exit 1
fi
command -v rg >/dev/null 2>&1 || { echo "rg is required" >&2; exit 1; }

files=()
while IFS= read -r path; do
  if rg -q '\bturbo_[A-Za-z_][A-Za-z0-9_]*' "$path"; then
    files+=("$path")
  fi
done < <("$FD" --type f --extension c --extension h . turbostl/src | sort)

printf '%s\n' "${files[@]}"

if ((${#files[@]})); then
  perl -pi -e 's/\bturbo_([A-Za-z_][A-Za-z0-9_]*)/$1/g' "${files[@]}"
fi

if rg -n '\bturbo_[A-Za-z_][A-Za-z0-9_]*' turbostl/src --glob '*.{c,h}'; then
  echo "legacy turbo_ identifiers remain in turbostl/src" >&2
  exit 1
fi

git diff --check
