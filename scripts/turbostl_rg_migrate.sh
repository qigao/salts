#!/usr/bin/env bash
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"

command -v rg >/dev/null 2>&1 || { echo "rg is required" >&2; exit 1; }

echo '=== legacy surface before ==='
rg -n '\bturbo_[A-Za-z0-9_]+|\bturbostl_status\b|\bturbo_stl_status\b|TURBO_STL_(OK|INVALID_ARGUMENT|OUT_OF_MEMORY|CAPACITY_EXCEEDED|EMPTY|NOT_FOUND|TYPE_MISMATCH|TRAIT_MISSING)' \
  turbostl utils turbo_serial cmeta cflow -g '*.[ch]' -g '*.cpp' -g '*.hpp' || true

python3 - <<'PY'
from pathlib import Path
import re

public_dir = Path('turbostl/include/turbostl')
headers = sorted(public_dir.glob('*.h'))

symbol_map = {
    'turbostl_status': 'stl_status',
    'turbo_stl_status': 'stl_status',
    'TURBO_STL_OK': 'STL_OK',
    'TURBO_STL_INVALID_ARGUMENT': 'STL_INVALID_ARGUMENT',
    'TURBO_STL_OUT_OF_MEMORY': 'STL_OUT_OF_MEMORY',
    'TURBO_STL_CAPACITY_EXCEEDED': 'STL_CAPACITY_EXCEEDED',
    'TURBO_STL_EMPTY': 'STL_EMPTY',
    'TURBO_STL_NOT_FOUND': 'STL_NOT_FOUND',
    'TURBO_STL_TYPE_MISMATCH': 'STL_TYPE_MISMATCH',
    'TURBO_STL_TRAIT_MISSING': 'STL_TRAIT_MISSING',
    'TURBO_BTREE_DEFAULT_MIN_DEGREE': 'BTREE_DEFAULT_MIN_DEGREE',
    'TURBO_BPLUS_TREE_DEFAULT_MIN_DEGREE': 'BPLUS_TREE_DEFAULT_MIN_DEGREE',
}
source_renames = {}

# Read the temporary public alias map before removing it. It is the migration
# table for repository consumers, not a compatibility surface to retain.
for h in headers:
    text = h.read_text(encoding='utf-8')
    for old, target in re.findall(
            r'^#define\s+(turbo_[A-Za-z0-9_]+)\s+([A-Za-z_][A-Za-z0-9_]*)\s*$',
            text, re.M):
        symbol_map[old] = target
        natural = old[len('turbo_'):]
        if target != natural and '_raw_' in target:
            source_renames.setdefault(h.stem, {})[natural] = target
    for natural, old in re.findall(
            r'^typedef\s+([A-Za-z_][A-Za-z0-9_]*)\s+(turbo_[A-Za-z0-9_]+)\s*;\s*$',
            text, re.M):
        symbol_map[old] = natural

# Implementation symbols must be the raw names consumed by the new natural
# instance wrappers. Do this before repository call-site migration.
for stem, replacements in source_renames.items():
    src = Path('turbostl/src') / f'{stem}.c'
    if not src.exists():
        continue
    text = src.read_text(encoding='utf-8')
    for old, new in sorted(replacements.items(), key=lambda item: -len(item[0])):
        text = re.sub(r'\b' + re.escape(old) + r'\b', new, text)
    src.write_text(text, encoding='utf-8')

# Status spelling is implementation-wide, not a public compatibility alias.
for p in Path('turbostl/src').glob('*'):
    if p.suffix not in {'.c', '.h'}:
        continue
    text = p.read_text(encoding='utf-8')
    text = re.sub(r'\bturbostl_status\b', 'stl_status', text)
    text = re.sub(r'\bturbo_stl_status\b', 'stl_status', text)
    for old, new in {
        'TURBO_STL_OK': 'STL_OK',
        'TURBO_STL_INVALID_ARGUMENT': 'STL_INVALID_ARGUMENT',
        'TURBO_STL_OUT_OF_MEMORY': 'STL_OUT_OF_MEMORY',
        'TURBO_STL_CAPACITY_EXCEEDED': 'STL_CAPACITY_EXCEEDED',
        'TURBO_STL_EMPTY': 'STL_EMPTY',
        'TURBO_STL_NOT_FOUND': 'STL_NOT_FOUND',
        'TURBO_STL_TYPE_MISMATCH': 'STL_TYPE_MISMATCH',
        'TURBO_STL_TRAIT_MISSING': 'STL_TRAIT_MISSING',
    }.items():
        text = re.sub(r'\b' + old + r'\b', new, text)
    p.write_text(text, encoding='utf-8')

# The tree implementations already use natural internal type names. Make the
# public handle layout use those exact types instead of legacy turbo_ tags and
# typedef names. Replace the typedef spellings first because `_t` is part of
# the identifier and therefore cannot be reached by a word-boundary tag-only
# replacement.
for name, replacements in {
    'btree.h': {
        'turbo_btree_entry_link_t': 'btree_entry_link_t',
        'turbo_btree_node_t': 'btree_node_t',
        'turbo_btree_entry_link': 'btree_entry_link',
        'turbo_btree_node': 'btree_node',
    },
    'bplus_tree.h': {
        'turbo_bplus_tree_entry_link_t': 'bplus_tree_entry_link_t',
        'turbo_bplus_tree_node_t': 'bplus_tree_node_t',
        'turbo_bplus_tree_entry_link': 'bplus_tree_entry_link',
        'turbo_bplus_tree_node': 'bplus_tree_node',
    },
}.items():
    p = public_dir / name
    text = p.read_text(encoding='utf-8')
    for old, new in sorted(replacements.items(), key=lambda item: -len(item[0])):
        text = re.sub(r'\b' + re.escape(old) + r'\b', new, text)
    p.write_text(text, encoding='utf-8')

# Migrate every repository consumer of the temporary TurboSTL aliases. Only
# aliases actually declared by TurboSTL are rewritten, so unrelated turbo_ APIs
# in other modules are untouched.
roots = [Path('turbostl'), Path('utils'), Path('turbo_serial'), Path('cmeta'),
         Path('cflow'), Path('stream')]
files = []
for root in roots:
    if not root.exists():
        continue
    for p in root.rglob('*'):
        if p.is_file() and p.suffix in {'.c', '.h', '.cc', '.cpp', '.cxx', '.hpp'}:
            if public_dir in p.parents:
                continue
            files.append(p)

for p in files:
    text = p.read_text(encoding='utf-8')
    original = text
    for old, new in sorted(symbol_map.items(), key=lambda item: -len(item[0])):
        text = re.sub(r'\b' + re.escape(old) + r'\b', new, text)
    if text != original:
        p.write_text(text, encoding='utf-8')

# Remove the alias blocks themselves. They are migration input, never final API.
# Accept both "alias" and "aliases" so one-off surfaces such as sort.h are not
# accidentally retained.
for p in headers:
    text = p.read_text(encoding='utf-8')
    text = re.sub(
        r'\n/\* Temporary repository-migration alias(?:es)?\.[\s\S]*?(?=\n#ifdef __cplusplus)',
        '\n', text)
    # Naturalize the two remaining tree configuration names.
    text = re.sub(r'\bTURBO_BTREE_DEFAULT_MIN_DEGREE\b',
                  'BTREE_DEFAULT_MIN_DEGREE', text)
    text = re.sub(r'\bTURBO_BPLUS_TREE_DEFAULT_MIN_DEGREE\b',
                  'BPLUS_TREE_DEFAULT_MIN_DEGREE', text)
    p.write_text(text, encoding='utf-8')

# status.h has a dedicated migration bridge rather than the generic alias block.
Path('turbostl/include/turbostl/status.h').write_text('''#ifndef TURBOSTL_STATUS_H\n#define TURBOSTL_STATUS_H\n\ntypedef enum stl_status {\n  STL_OK = 0,\n  STL_INVALID_ARGUMENT,\n  STL_OUT_OF_MEMORY,\n  STL_CAPACITY_EXCEEDED,\n  STL_EMPTY,\n  STL_NOT_FOUND,\n  STL_TYPE_MISMATCH,\n  STL_TRAIT_MISSING\n} stl_status;\n\n#endif /* TURBOSTL_STATUS_H */\n''', encoding='utf-8')
PY

echo '=== public turbo_ audit ==='
if rg -n '\bturbo_[A-Za-z0-9_]+' turbostl/include; then
  echo 'legacy public turbo_ surface remains' >&2
  exit 1
fi

echo '=== generated/legacy facade audit ==='
if rg -n 'legacy_generated_typed|legacy_[A-Za-z0-9_]*test|detail/typed_facade|<turbostl/meta\.h>' turbostl; then
  echo 'generated/legacy facade remains' >&2
  exit 1
fi

echo '=== old status audit ==='
if rg -n '\bturbostl_status\b|\bturbo_stl_status\b|TURBO_STL_(OK|INVALID_ARGUMENT|OUT_OF_MEMORY|CAPACITY_EXCEEDED|EMPTY|NOT_FOUND|TYPE_MISMATCH|TRAIT_MISSING)' \
    turbostl utils turbo_serial cmeta cflow -g '*.[ch]' -g '*.cpp' -g '*.hpp'; then
  echo 'legacy status spelling remains' >&2
  exit 1
fi

echo '=== tree internal type audit ==='
if rg -n '\bturbo_(btree|bplus_tree)_(node|entry_link)' turbostl; then
  echo 'legacy tree internal types remain' >&2
  exit 1
fi

git diff --check