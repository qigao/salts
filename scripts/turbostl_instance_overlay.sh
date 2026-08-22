#!/usr/bin/env bash
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

command -v rg >/dev/null 2>&1 || { echo "rg is required" >&2; exit 1; }

REMOTE_REF=origin/refactor/execution-foundation
git fetch --no-tags origin refactor/execution-foundation:refs/remotes/origin/refactor/execution-foundation

# Import only the already-implemented self-describing TurboSTL contract plus
# the CMeta entry primitive it depends on. Do not import execution-foundation
# Platform/Concurrency/CFlow changes.
git checkout "$REMOTE_REF" -- \
  turbostl/include/turbostl.h \
  turbostl/include/turbostl \
  turbostl/src/instance_meta.c \
  turbostl/src/associative_meta.c \
  cmeta/include/cmeta/entry.h \
  cmeta/src/entry.c \
  cmeta/include/cmeta/meta.h \
  cmeta/CMakeLists.txt

rm -f turbostl/include/turbostl/detail/typed_facade.h

# Keep the already-natural compiled algorithm sources in this branch. Their
# typed init/from/destroy entry points now sit behind explicit *_raw_* names.
python3 - <<'PY'
from pathlib import Path
import re

headers = sorted(Path('turbostl/include/turbostl').glob('*.h'))
source_by_prefix = {}
for p in Path('turbostl/src').glob('*.c'):
    if p.name in {'instance_meta.c', 'associative_meta.c'}:
        continue
    source_by_prefix[p.stem] = p

for h in headers:
    text = h.read_text(encoding='utf-8')
    prefix = h.stem
    src = source_by_prefix.get(prefix)
    if src is None:
        continue
    replacements = []
    for macro, target in re.findall(r'^#define\s+(turbo_[A-Za-z0-9_]+)\s+([A-Za-z_][A-Za-z0-9_]*)\s*$', text, re.M):
        natural = macro[len('turbo_'):]
        if target != natural and '_raw_' in target:
            replacements.append((natural, target))
    if not replacements:
        continue
    s = src.read_text(encoding='utf-8')
    for old, new in sorted(replacements, key=lambda x: -len(x[0])):
        s = re.sub(r'\b' + re.escape(old) + r'\b', new, s)
    src.write_text(s, encoding='utf-8')

# Current branch already removed the old turbo_ implementation prefixes.
# Normalize the imported metadata sources/status spelling to the same final
# implementation vocabulary.
for p in list(Path('turbostl/src').glob('*.c')) + list(Path('utils').rglob('*.[ch]')) + list(Path('turbo_serial').rglob('*.[ch]')):
    try:
        s = p.read_text(encoding='utf-8')
    except (UnicodeDecodeError, OSError):
        continue
    s2 = s.replace('turbostl_status', 'stl_status')
    if s2 != s:
        p.write_text(s2, encoding='utf-8')
PY

# List's adapter was still header-local in the source branch. Move the exact
# existing adapter body into the compiled metadata TU and export one canonical
# descriptor, matching Map's compiled descriptor model.
python3 - <<'PY'
from pathlib import Path

list_h = Path('turbostl/include/turbostl/list.h')
text = list_h.read_text(encoding='utf-8')
start_marker = '/* Instance-driven CMeta adapter. */'
end_marker = '/* Temporary repository-migration aliases. */'
start = text.index(start_marker)
end = text.index(end_marker)
adapter = text[start:end].rstrip() + '\n'
adapter = adapter.replace(
    'static const cmeta_container_desc stl_list_container_desc =',
    'const cmeta_container_desc stl_list_container_desc =')
text = text[:start] + text[end:]
list_h.write_text(text, encoding='utf-8')

meta_c = Path('turbostl/src/instance_meta.c')
meta = meta_c.read_text(encoding='utf-8').rstrip() + '\n\n' + adapter + '\n'
meta_c.write_text(meta, encoding='utf-8')

meta_h = Path('turbostl/include/turbostl/detail/instance_meta.h')
h = meta_h.read_text(encoding='utf-8')
needle = 'extern const cmeta_container_desc stl_vec_container_desc;\n'
if 'stl_list_container_desc' not in h:
    h = h.replace(needle, needle + 'extern const cmeta_container_desc stl_list_container_desc;\n')
meta_h.write_text(h, encoding='utf-8')
PY

# Add the compiled descriptor TUs without replacing the current natural source list.
python3 - <<'PY'
from pathlib import Path
p = Path('turbostl/CMakeLists.txt')
s = p.read_text(encoding='utf-8')
needle = 'add_library(${TARGET_NAME}\n'
if 'src/instance_meta.c' not in s:
    s = s.replace(needle, needle + '  src/instance_meta.c\n  src/associative_meta.c\n')
p.write_text(s, encoding='utf-8')
PY

# The imported public model has no generated facade. Keep its migration aliases
# just long enough for the existing repository tests/consumers to compile; the
# next rg pass removes them after call sites move.
rg -n 'detail/typed_facade|<turbostl/meta\.h>|legacy_generated_typed' \
  turbostl/include turbostl/src || true

git diff --check
