#!/usr/bin/env bash
# Regression runner for Breeze.
#
# Each tests/**/*.bz script declares its expectations in comments:
#   // expect: <line>                 expected stdout line (in order)
#   // expect runtime error: <text>   stderr must contain <text>, exit code 70
#   // expect compile error: <text>   stderr must contain <text>, exit code 65
#   // repl                           feed the script to the REPL on stdin
#                                     (prompts are stripped from stdout)
#
# Any AddressSanitizer / UBSan / LeakSanitizer report fails the test.
#
# Usage: tests/run.sh [filter]       (filter is a substring of the test path)
# Env:   BREEZE=<binary>             skip the build and use this binary
#        CFLAGS_EXTRA="-D..."        extra flags for the test build

set -u

root="$(cd "$(dirname "$0")/.." && pwd)"
filter="${1:-}"

if [[ -z "${BREEZE:-}" ]]; then
  BREEZE="$root/build/breeze-test"
  mkdir -p "$root/build"
  # shellcheck disable=SC2086
  if ! gcc -std=c2x -Wall -Wextra -g -fsanitize=address,undefined \
      -fno-sanitize-recover=undefined ${CFLAGS_EXTRA:-} \
      -I"$root/src" "$root"/src/*.c -o "$BREEZE" 2> "$root/build/test-build.log"; then
    cat "$root/build/test-build.log" >&2
    echo "build failed" >&2
    exit 1
  fi
fi

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=0"
export UBSAN_OPTIONS="print_stacktrace=1"

pass=0
fail=0
failed=()

while IFS= read -r test; do
  rel="${test#"$root"/}"
  [[ -n "$filter" && "$rel" != *"$filter"* ]] && continue

  expected_out="$(sed -n 's|.*// expect: \(.*\)$|\1|p' "$test")"
  runtime_err="$(sed -n 's|.*// expect runtime error: \(.*\)$|\1|p' "$test" | head -n1)"
  compile_err="$(sed -n 's|.*// expect compile error: \(.*\)$|\1|p' "$test" | head -n1)"

  expected_code=0
  [[ -n "$runtime_err" ]] && expected_code=70
  [[ -n "$compile_err" ]] && expected_code=65

  out_file="$(mktemp)"
  err_file="$(mktemp)"
  if grep -q '^// repl' "$test"; then
    "$BREEZE" < "$test" > "$out_file" 2> "$err_file"
    code=$?
    # REPL mode never exits with an error code; strip the ">> " prompts.
    expected_code=0
    actual_out="$(sed -E 's/^(>> )+//' "$out_file" | sed '/^$/d')"
  else
    "$BREEZE" "$test" > "$out_file" 2> "$err_file"
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
  if [[ -n "$runtime_err" && "$actual_err" != *"$runtime_err"* ]]; then
    problems+=("missing runtime error: $runtime_err")
  fi
  if [[ -n "$compile_err" && "$actual_err" != *"$compile_err"* ]]; then
    problems+=("missing compile error: $compile_err")
  fi

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
