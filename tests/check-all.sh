#!/usr/bin/env bash
# Runs every check a change must pass: warnings, the suite under sanitizers,
# under GC stress, and against an optimized build without sanitizers.
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$root/build"

echo "== warnings (-Wall -Wextra -pedantic -Werror)"
gcc -std=c2x -Wall -Wextra -pedantic -Werror -fsyntax-only \
  -I"$root/src" "$root"/src/*.c

echo "== default (ASan + UBSan)"
"$root/tests/run.sh"

echo "== DEBUG_STRESS_GC"
CFLAGS_EXTRA=-DDEBUG_STRESS_GC "$root/tests/run.sh"

echo "== -O2 without sanitizers"
gcc -std=c2x -O2 -I"$root/src" "$root"/src/*.c -o "$root/build/breeze-o2"
BREEZE="$root/build/breeze-o2" "$root/tests/run.sh"

echo "== switch dispatch (portable fallback)"
gcc -std=c2x -Wall -Wextra -pedantic -Werror -fsyntax-only \
  -DBREEZE_SWITCH_DISPATCH -I"$root/src" "$root"/src/*.c
CFLAGS_EXTRA=-DBREEZE_SWITCH_DISPATCH "$root/tests/run.sh"

echo "== all checks passed"
