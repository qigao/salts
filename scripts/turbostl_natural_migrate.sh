#!/usr/bin/env bash
set -euo pipefail

ROOT="$(git rev-parse --show-toplevel)"
cd "$ROOT"

if command -v fd >/dev/null 2>&1; then
  FD=fd
elif command -v fdfind >/dev/null 2>&1; then
  FD=fdfind
else
  echo "fd/fdfind is required" >&2
  exit 1
fi

command -v rg >/dev/null 2>&1 || { echo "rg is required" >&2; exit 1; }

while IFS= read -r path; do
  base="$(basename "$path")"
  dir="$(dirname "$path")"
  new="$dir/${base#turbo_}"
  if [[ "$path" != "$new" ]]; then
    git mv "$path" "$new"
  fi
done < <("$FD" '^turbo_.*\.c$' turbostl/src --type f | sort)

for path in turbostl/src/turbo_rbtree_internal.h turbostl/src/turbo_sequence_internal.h; do
  if [[ -f "$path" ]]; then
    git mv "$path" "${path/turbo_/}"
  fi
done

pattern='turbo_(bplus_tree|btree|deque|hash_map|hash_set|heap|list|map|multimap|queue|rbtree|sequence|set|sort|stack|vec)|turbo_stl_status'
mapfile -t files < <(rg -l "$pattern" \
  --glob '!vendor/**' \
  --glob '!build/**' \
  --glob '!vcpkg_installed/**' \
  --glob '!.codegraph/**' \
  --glob '!scripts/turbostl_natural_migrate.sh' \
  --glob '!docs/superpowers/plans/**' \
  --glob '!docs/superpowers/specs/**' \
  . | sort)

python3 - "${files[@]}" <<'PY'
from pathlib import Path
import sys

pairs = [
    ("turbo_bplus_tree", "bplus_tree"),
    ("turbo_hash_map", "hash_map"),
    ("turbo_hash_set", "hash_set"),
    ("turbo_multimap", "multimap"),
    ("turbo_sequence", "sequence"),
    ("turbo_rbtree", "rbtree"),
    ("turbo_btree", "btree"),
    ("turbo_deque", "deque"),
    ("turbo_heap", "heap"),
    ("turbo_list", "list"),
    ("turbo_map", "map"),
    ("turbo_queue", "queue"),
    ("turbo_set", "set"),
    ("turbo_sort", "sort"),
    ("turbo_stack", "stack"),
    ("turbo_vec", "vec"),
    ("turbo_stl_status", "turbostl_status"),
]

for arg in sys.argv[1:]:
    path = Path(arg)
    if not path.is_file():
        continue
    try:
        text = path.read_text(encoding="utf-8")
    except UnicodeDecodeError:
        continue
    new = text
    for old, repl in pairs:
        new = new.replace(old, repl)
    if new != text:
        path.write_text(new, encoding="utf-8")
PY

python3 - <<'PY'
from pathlib import Path
p = Path("turbostl/include/turbostl/typed.h")
text = p.read_text(encoding="utf-8")
start = text.find("/* Semantic front-end calls.")
end = text.rfind("#endif")
if start != -1 and end != -1 and start < end:
    text = text[:start].rstrip() + "\n\n" + text[end:]
    p.write_text(text, encoding="utf-8")
PY

if "$FD" '^turbo_.*\.c$' turbostl/src --type f | rg -q '.'; then
  echo "legacy turbo_*.c files remain" >&2
  "$FD" '^turbo_.*\.c$' turbostl/src --type f >&2
  exit 1
fi

rg -n 'turbo_' turbostl/include --glob '*.h' || true

git diff --check
