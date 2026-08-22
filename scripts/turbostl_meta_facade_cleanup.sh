#!/usr/bin/env bash
set -euo pipefail
cd "$(git rev-parse --show-toplevel)"
command -v rg >/dev/null 2>&1 || { echo "rg is required" >&2; exit 1; }

mkdir -p turbostl/include/turbostl/detail
if [[ -f turbostl/include/turbostl/meta.h ]]; then
  git mv turbostl/include/turbostl/meta.h \
    turbostl/include/turbostl/detail/typed_facade.h
fi

mapfile -t files < <(rg -l '<turbostl/meta\.h>|TURBO_STL_META_H' \
  turbostl cmeta cflow utils turbo_serial \
  --glob '*.{h,c,cpp,hpp,cmake,txt}' | sort)

if ((${#files[@]})); then
  perl -pi -e 's#<turbostl/meta\.h>#<turbostl/detail/typed_facade.h>#g; s/TURBO_STL_META_H/TURBOSTL_DETAIL_TYPED_FACADE_H/g' "${files[@]}"
fi

if [[ -e turbostl/include/turbostl/meta.h ]]; then
  echo "turbostl/meta.h still exists" >&2
  exit 1
fi
if rg -n '<turbostl/meta\.h>|TURBO_STL_META_H' turbostl cmeta cflow utils turbo_serial; then
  echo "old TurboSTL meta facade references remain" >&2
  exit 1
fi

git diff --check
