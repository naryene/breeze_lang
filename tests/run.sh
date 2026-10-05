#!/usr/bin/env bash
# Regression runner for Breeze.
#
# Each tests/**/*.bz script declares its expectations in comments:
#   // expect: <line>                 expected stdout line (in order)
#   // expect runtime error: <text>   stderr must contain <text>, exit code 70
#   // expect compile error: <text>   stderr must contain <text>, exit code 65
#                                     (repeatable: every line must appear)
#   // repl                           feed the script to the REPL on stdin
#                                     (prompts are stripped from stdout)
#   // stress-gc                      run with a build that collects garbage
#                                     on every allocation (DEBUG_STRESS_GC)
#
# Any AddressSanitizer / UBSan / LeakSanitizer report fails the test.
#
# Usage: tests/run.sh [filter]       (filter is a substring of the test path)
# Env:   BREEZE=<binary>             skip the build and use this binary
#        CFLAGS_EXTRA="-D..."        extra flags for the test build

set -u

root="$(cd "$(dirname "$0")/.." && pwd)"
filter="${1:-}"

# build <output> [extra cflags...]
build() {
  local out="$1"
  shift
  mkdir -p "$root/build"
  # shellcheck disable=SC2086
  if ! gcc -std=c2x -Wall -Wextra -g -fsanitize=address,undefined \
      -fno-sanitize-recover=undefined ${CFLAGS_EXTRA:-} "$@" \
      -I"$root/src" "$root"/src/*.c -o "$out" 2> "$root/build/test-build.log"; then
    cat "$root/build/test-build.log" >&2
    echo "build failed" >&2
    exit 1
  fi
}

if [[ -z "${BREEZE:-}" ]]; then
  BREEZE="$root/build/breeze-test"
  build "$BREEZE"
fi
BREEZE_STRESS="${BREEZE_STRESS:-}"

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=0"
export UBSAN_OPTIONS="print_stacktrace=1"

pass=0
fail=0
failed=()

while IFS= read -r test; do
  rel="${test#"$root"/}"
  [[ -n "$filter" && "$rel" != *"$filter"* ]] && continue

  expected_out="$(sed -n 's|.*// expect: \(.*\)$|\1|p' "$test")"
  runtime_err="$(sed -n 's|.*// expect runtime error: \(.*\)$|\1|p' "$test")"
  compile_err="$(sed -n 's|.*// expect compile error: \(.*\)$|\1|p' "$test")"

  expected_code=0
  [[ -n "$runtime_err" ]] && expected_code=70
  [[ -n "$compile_err" ]] && expected_code=65

  bin="$BREEZE"
  if grep -q '^// stress-gc' "$test"; then
    if [[ -z "$BREEZE_STRESS" ]]; then
      BREEZE_STRESS="$root/build/breeze-test-stress"
      build "$BREEZE_STRESS" -DDEBUG_STRESS_GC
    fi
    bin="$BREEZE_STRESS"
  fi

  out_file="$(mktemp)"
  err_file="$(mktemp)"
  if grep -q '^// repl' "$test"; then
    "$bin" < "$test" > "$out_file" 2> "$err_file"
    code=$?
    # REPL mode never exits with an error code; strip the ">> " prompts.
    expected_code=0
    actual_out="$(sed -E 's/^(>> )+//' "$out_file" | sed '/^$/d')"
  else
    "$bin" "$test" > "$out_file" 2> "$err_file"
    code=$?
    actual_out="$(cat "$out_file")"
  fi
  actual_err="$(cat "$err_file")"
  rm -f "$out_file" "$err_file"

  problems=()
  if grep -Eq 'ERROR: [A-Za-z]*Sanitizer|\.c:[0-9]+:[0-9]+: runtime error:' <<< "$actual_err"; then
    problems+=("sanitizer report")
  fi
  if [[ "$code" -ne "$expected_code" ]]; then
    problems+=("exit code $code, expected $expected_code")
  fi
  if [[ "$actual_out" != "$expected_out" ]]; then
    problems+=("stdout mismatch")
  fi
  # Every expected error line must appear somewhere in stderr.
  while IFS= read -r want; do
    [[ -n "$want" && "$actual_err" != *"$want"* ]] &&
      problems+=("missing runtime error: $want")
  done <<< "$runtime_err"
  while IFS= read -r want; do
    [[ -n "$want" && "$actual_err" != *"$want"* ]] &&
      problems+=("missing compile error: $want")
  done <<< "$compile_err"

  if [[ ${#problems[@]} -eq 0 ]]; then
    pass=$((pass + 1))
  else
    fail=$((fail + 1))
    failed+=("$rel")
    echo "FAIL $rel"
    for p in "${problems[@]}"; do echo "  - $p"; done
    if [[ "$actual_out" != "$expected_out" ]]; then
      diff <(echo "$expected_out") <(echo "$actual_out") \
        --label expected --label actual -u | sed 's/^/    /' | head -n 20
    fi
    if [[ -n "$actual_err" ]]; then
      echo "  stderr:"
      head -n 8 <<< "$actual_err" | sed 's/^/    /'
    fi
  fi
done < <(find "$root/tests" -name '*.bz' | sort)

echo
echo "passed: $pass  failed: $fail"
[[ "$fail" -eq 0 ]]
