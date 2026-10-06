#!/usr/bin/env bash
# Regression runner for Breeze.
#
# Each tests/**/*.bz script declares its expectations in comments:
#   // expect: <line>                 expected stdout line (in order)
#   // expect contains: <text>        stdout must contain <text>
#   // expect not contains: <text>    stdout must not contain <text>
#                                     (with either of these, stdout is not
#                                     compared line by line)
#   // expect runtime error: <text>   stderr must contain <text>, exit code 70
#   // expect compile error: <text>   stderr must contain <text>, exit code 65
#                                     (repeatable: every line must appear)
#   // repl                           feed the script to the REPL on stdin
#                                     (prompts are stripped from stdout)
#   // cflags: <flags>                run on a build with extra compiler flags,
#                                     e.g. -DDEBUG_PRINT_CODE
#   // stress-gc                      shorthand for: // cflags: -DDEBUG_STRESS_GC
#
# Any AddressSanitizer / UBSan / LeakSanitizer report fails the test.
#
# Usage: tests/run.sh [filter]       (filter is a substring of the test path)
# Env:   BREEZE=<binary>             use this binary for tests without cflags
#        CFLAGS_EXTRA="-D..."        extra flags for every test build

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

# One sanitizer build per distinct cflags string, built on first use. The
# result is returned in $flag_bin (not via $(...), which would run in a
# subshell and lose the cache).
declare -A flag_builds=()
flag_bin=""
select_flag_binary() {
  local flags="$1"
  if [[ -z "${flag_builds[$flags]:-}" ]]; then
    local name
    name="$(tr -c 'A-Za-z0-9\n' '_' <<< "$flags")"
    local out="$root/build/breeze-test-$name"
    # shellcheck disable=SC2086
    build "$out" $flags
    flag_builds[$flags]="$out"
  fi
  flag_bin="${flag_builds[$flags]}"
}

export ASAN_OPTIONS="detect_leaks=1:abort_on_error=0"
export UBSAN_OPTIONS="print_stacktrace=1"

pass=0
fail=0

while IFS= read -r test; do
  rel="${test#"$root"/}"
  [[ -n "$filter" && "$rel" != *"$filter"* ]] && continue

  expected_out="$(sed -n 's|.*// expect: \(.*\)$|\1|p' "$test")"
  contains="$(sed -n 's|.*// expect contains: \(.*\)$|\1|p' "$test")"
  not_contains="$(sed -n 's|.*// expect not contains: \(.*\)$|\1|p' "$test")"
  runtime_err="$(sed -n 's|.*// expect runtime error: \(.*\)$|\1|p' "$test")"
  compile_err="$(sed -n 's|.*// expect compile error: \(.*\)$|\1|p' "$test")"

  expected_code=0
  [[ -n "$runtime_err" ]] && expected_code=70
  [[ -n "$compile_err" ]] && expected_code=65

  flags="$(sed -n 's|^// cflags: \(.*\)$|\1|p' "$test" | head -n1)"
  grep -q '^// stress-gc' "$test" && flags="$flags -DDEBUG_STRESS_GC"
  flags="$(xargs <<< "$flags")"
  bin="$BREEZE"
  if [[ -n "$flags" ]]; then
    select_flag_binary "$flags"
    bin="$flag_bin"
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

  exact_stdout=true
  if [[ -n "$contains" || -n "$not_contains" ]]; then
    exact_stdout=false
    while IFS= read -r want; do
      [[ -n "$want" && "$actual_out" != *"$want"* ]] &&
        problems+=("stdout missing: $want")
    done <<< "$contains"
    while IFS= read -r unwanted; do
      [[ -n "$unwanted" && "$actual_out" == *"$unwanted"* ]] &&
        problems+=("stdout unexpectedly contains: $unwanted")
    done <<< "$not_contains"
  elif [[ "$actual_out" != "$expected_out" ]]; then
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
    echo "FAIL $rel"
    for p in "${problems[@]}"; do echo "  - $p"; done
    if $exact_stdout && [[ "$actual_out" != "$expected_out" ]]; then
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
