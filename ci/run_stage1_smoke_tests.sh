#!/usr/bin/env bash

set -u

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$repo_root" || exit 1

cmake --build build || exit 1

export HOME=/tmp
export XDG_CACHE_HOME=/tmp
export LD_LIBRARY_PATH="$repo_root/deps/core/lib:/opt/Baltamatica/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

tests=(
  "test/simple_demo.m"
  "test/test1/test1.m"
  "test/test1_2/test1_2.m"
  "test/test1_3/test1_3.m"
  "test/test1_4/test1_4.m"
  "test/test1_5/test1_5.m"
)

failures=0

for test_file in "${tests[@]}"; do
  printf '==> %s\n' "$test_file"
  if ./build/main "$test_file"; then
    printf 'PASS %s\n' "$test_file"
  else
    printf 'FAIL %s\n' "$test_file" >&2
    failures=$((failures + 1))
  fi
done

if (( failures != 0 )); then
  printf '\n%d test(s) failed.\n' "$failures" >&2
  exit 1
fi

printf '\nAll stage1 smoke tests passed.\n'
