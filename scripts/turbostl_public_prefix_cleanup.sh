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

pattern='\b(turbo_hash_fn|turbo_hash_equal_fn|turbo_hash_bytes|turbo_hash_key_equal|turbo_stable_sort|turbo_stl_cmeta_status)\b'
mapfile -t files < <(rg -l "$pattern" . \
  --glob '!vendor/**' \
  --glob '!build/**' \
  --glob '!vcpkg_installed/**' \
  --glob '!.codegraph/**' \
  --glob '!docs/**' \
  --glob '!scripts/turbostl_public_prefix_cleanup.sh' \
  | sort)
printf '%s\n' "${files[@]}"

python3 - "${files[@]}" <<'PY'
from pathlib import Path
import sys
pairs = [
    ("turbo_hash_equal_fn", "hash_equal_fn"),
    ("turbo_hash_key_equal", "hash_key_equal"),
    ("turbo_hash_bytes", "hash_bytes"),
    ("turbo_hash_fn", "hash_fn"),
    ("turbo_stable_sort", "stable_sort"),
    ("turbo_stl_cmeta_status", "turbostl_cmeta_status"),
]
for name in sys.argv[1:]:
    p = Path(name)
    text = p.read_text(encoding="utf-8")
    new = text
    for old, repl in pairs:
        new = new.replace(old, repl)
    if new != text:
        p.write_text(new, encoding="utf-8")
PY

if rg -n "$pattern" . \
  --glob '!vendor/**' \
  --glob '!build/**' \
  --glob '!vcpkg_installed/**' \
  --glob '!.codegraph/**' \
  --glob '!docs/**' \
  --glob '!scripts/turbostl_public_prefix_cleanup.sh'; then
  echo "legacy TurboSTL public identifiers remain" >&2
  exit 1
fi

git diff --check
