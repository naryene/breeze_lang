# Breeze Interpreter Fast Path Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make the Breeze bytecode interpreter at least 1.4× faster (geometric mean over `bench/`) and faster than CPython on every benchmark, without changing language behaviour.

**Architecture:** Phase 1 of the performance roadmap. Every change stays inside the existing single-pass compiler → stack VM design: cheaper encodings (one-byte slot operands, fused pop instructions), cheaper hashing (bit masks), a register-resident instruction pointer, and computed-goto dispatch. Each task is measured against the previous one with the benchmark harness and must keep the regression suite green.

**Tech Stack:** C23 (`gcc -std=c2x`), GNU extensions only behind `#if` guards, bash test runner, Python 3 benchmark harness.

**Spec:** `docs/superpowers/specs/2026-10-06-breeze-performance-roadmap.md`

## Global Constraints

- Compiler: `gcc -std=c2x`. The tree must build with zero warnings under `-Wall -Wextra -pedantic`. The only exception is computed-goto dispatch, whose GNU-only syntax is wrapped in `#pragma GCC diagnostic` push/pop.
- A portable build must keep working: `-DBREEZE_SWITCH_DISPATCH` selects the `switch` dispatcher.
- No new dependencies. Benchmarks use only runtimes already installed.
- Language semantics must not change: strict booleans, declared fields, error messages, and `[line N] in <fn>()` traces stay byte-for-byte identical.
- Limits stay as they are: 256 local slots and 256 upvalues per function, 24-bit constant indices, absolute 16-bit jump targets.
- Commit messages follow Conventional Commits, `<type>(<scope>): <summary>`. Never add a `Co-Authored-By:` trailer and never add "Generated with" lines to PR descriptions.
- Code style matches the codebase: `snake_case` functions, `PascalCase` types and opcodes, `+= 1` instead of `++`.
- Every task ends with `tests/check-all.sh` passing. Every performance task (3–8) also records a benchmark and keeps the change only if no benchmark regresses by more than 3%.

## Review Focus

1. **Short-circuit operators that jump to the end of an assignment statement** (`true || (x = true);`). The fused `OpSetLocalPop` must not swallow the pop that the short-circuit path needs; later locals must still read the right slots. The test lives in Task 7.
2. **Runtime errors raised inside a callee after the caller has run for a while.** With the instruction pointer cached in a register, every frame in the trace must still report its current line, not the line where the frame started. The test lives in Task 5.
3. **Non-boolean `while`/`for` conditions** (`while (1) {}`). The new `OpJmpIfFalsePop` must still raise `Operand must be a boolean.` with the right line. The test lives in Task 6.
4. **Closures capturing locals in high slots (200+).** One-byte slot operands must not truncate or sign-extend indices near the 255 limit. The test lives in Task 4.
5. **Hash tables crossing resize boundaries** (hundreds of globals, dozens of fields, heavy string interning). Bit-mask indexing must find every entry after each growth. The tests live in Task 3.

---

## File Map

| File | Responsibility | Tasks |
|---|---|---|
| `bench/*.bz,*.ts,*.lua,*.py,*.rs`, `bench/run.py` | Cross-language benchmarks plus before/after comparison | 0 |
| `tests/run.sh` | Regression runner: per-test cflags, substring expectations | 1 |
| `tests/check-all.sh` (new) | Runs the suite in every required configuration | 1, 8 |
| `src/debug.c` | Disassembler, the measuring tool | 2, 4, 6, 7 |
| `src/table.c` | Hash tables and sets | 3 |
| `src/chunk.h` | Opcode enum | 6, 7 |
| `src/compiler.c` | Bytecode emission | 4, 6, 7 |
| `src/virtual_machine.c` | Dispatch loop | 4, 5, 6, 7, 8 |
| `CLAUDE.md`, roadmap spec | Documentation of encoding, dispatch, benchmarks | 0, 1, 4, 9 |

---

### Task 0: Branch setup, benchmark suite, baseline

**Files:**
- Modify: `bench/run.py` (add `--save` / `--compare`)
- Commit: `bench/` (currently untracked), `docs/superpowers/` (roadmap spec and this plan)
- Modify: `CLAUDE.md` (Benchmarks section)

**Interfaces:**
- Produces: `python3 bench/run.py --langs breeze --save <file.json> --compare <file.json>`. The JSON maps `"<benchmark>|<language label>"` to median seconds. `build/bench/baseline.json` is the Phase 1 baseline.

- [ ] **Step 1: Bring the branch up to date with `main`**

`perf/benchmarks` sits at `db1be5d`, which `main` (`1c22443`, the PR #3 merge) already contains.

```bash
git checkout perf/benchmarks
git merge --ff-only main
git log --oneline -1   # expect: 1c22443 fix: interpreter bug sweep with regression test suite (#3)
tests/run.sh           # expect: passed: 36  failed: 0
```

- [ ] **Step 2: Add `--save` and `--compare` to `bench/run.py`**

Add `import json` next to the other imports:

```python
import argparse
import json
import os
```

In `main()`, after the existing `parser.add_argument("--langs", default="")` line, add:

```python
    parser.add_argument("--save", default="",
                        help="write results as JSON to this path")
    parser.add_argument("--compare", default="",
                        help="JSON from an earlier --save; prints Breeze speedups")
```

At the end of `main()`, after the loop that prints the Markdown table, add:

```python
    if args.save:
        Path(args.save).parent.mkdir(parents=True, exist_ok=True)
        Path(args.save).write_text(json.dumps(
            {f"{b}|{l}": r for (b, l), r in results.items()
             if isinstance(r, float)}, indent=2) + "\n")
        print(f"\nsaved {args.save}", file=sys.stderr)

    if args.compare:
        before = json.loads(Path(args.compare).read_text())
        print()
        print(f"Breeze vs {args.compare} (speedup > 1 means faster now):")
        print()
        print("| benchmark | before | after | speedup |")
        print("|---|---|---|---|")
        ratios = []
        for name in names:
            old = before.get(f"{name}|breeze")
            new = results.get((name, "breeze"))
            if isinstance(old, float) and isinstance(new, float):
                ratios.append(old / new)
                print(f"| {name} | {old:.3f}s | {new:.3f}s | {old / new:.2f}× |")
        if ratios:
            print(f"| **geomean** | | | **{statistics.geometric_mean(ratios):.2f}×** |")
```

- [ ] **Step 3: Check that the harness runs and validates outputs**

Run: `python3 bench/run.py --langs breeze --only fib,loop --runs 3`
Expected: a two-row table with no `FAIL` cells. Every program's output is checked against `BENCHMARKS` before timing.

- [ ] **Step 4: Record the baseline**

Run: `python3 bench/run.py --langs breeze --runs 7 --save build/bench/baseline.json`
Expected: `saved build/bench/baseline.json`. The `startup` row is about 1 ms and the others are about 1–2 s each. `build/` is git-ignored, so the baseline stays local. Record the numbers in Task 9's report.

- [ ] **Step 5: Document benchmarks in `CLAUDE.md`**

Add this section after `## Build & run` and its bullet list:

```markdown
## Benchmarks

- `python3 bench/run.py` times `bench/<name>.{bz,ts,lua,py,rs}` on every installed runtime (Breeze -O2, Lua, LuaJIT, Python, Node/Bun/Deno running TypeScript, Rust -O3), checks each program's output, and prints medians.
- `--langs breeze --save build/bench/x.json` records a run, and `--compare build/bench/x.json` prints per-benchmark speedups plus the geometric mean. Use these around every performance change.
- Benchmark programs may only use features Breeze has: no arrays, no `%`, no number→string conversion.
- Performance roadmap: `docs/superpowers/specs/2026-10-06-breeze-performance-roadmap.md`.
```

- [ ] **Step 6: Commit**

```bash
git add bench docs/superpowers CLAUDE.md
git commit -m "test(bench): add cross-language benchmark suite and performance roadmap"
```

---

### Task 1: Test runner, per-test compiler flags and substring expectations

**Files:**
- Modify: `tests/run.sh` (full replacement below)
- Create: `tests/check-all.sh`
- Create: `tests/runner/contains.bz`, `tests/runner/cflags.bz`
- Modify: `CLAUDE.md` (tests bullet)

**Interfaces:**
- Produces, for later tasks:
  - `// cflags: <flags>`: the test runs on a sanitizer build with extra flags, e.g. `-DDEBUG_PRINT_CODE`.
  - `// expect contains: <text>` / `// expect not contains: <text>`: substring checks on stdout. When either is present, stdout is not compared line by line.
  - `tests/check-all.sh`: the single command every later task runs.

- [ ] **Step 1: Write the failing runner self-tests**

`tests/runner/contains.bz`:

```
// Self-test: substring expectations on stdout.
print "hello world";
print "abc";
// expect contains: lo wo
// expect not contains: xyz
```

`tests/runner/cflags.bz`:

```
// cflags: -DDEBUG_PRINT_CODE
// Self-test: per-test compiler flags. The disassembler only prints when
// DEBUG_PRINT_CODE is defined, so its header proves the flag reached the build.
print 1;
// expect contains: == code ==
// expect contains: OpPrint
```

- [ ] **Step 2: Run them and confirm they fail**

Run: `tests/run.sh runner`
Expected: `FAIL tests/runner/cflags.bz` and `FAIL tests/runner/contains.bz`, both with `stdout mismatch`, then `passed: 0  failed: 2`.

- [ ] **Step 3: Replace `tests/run.sh`**

```bash
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
```

- [ ] **Step 4: Create `tests/check-all.sh`**

```bash
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

echo "== all checks passed"
```

Run: `chmod +x tests/check-all.sh`

- [ ] **Step 5: Run the self-tests, then everything**

Run: `tests/run.sh runner`
Expected: `passed: 2  failed: 0`

Run: `tests/check-all.sh`
Expected: each section ends with `passed: 38  failed: 0`, then `== all checks passed`.

- [ ] **Step 6: Update the tests bullet in `CLAUDE.md`**

Replace the `- Tests:` bullet under `## Build & run` with:

```markdown
- Tests: `tests/check-all.sh` is the gate for every change. It checks warnings with `-Werror` and runs the suite under ASan/UBSan, under `DEBUG_STRESS_GC`, and against an `-O2` build. `tests/run.sh [substring]` runs the suite (or a subset) once. Expectations live in comments: `// expect: <stdout line>`, `// expect contains: <text>` / `// expect not contains: <text>` (substring checks, which disable line-by-line comparison), `// expect runtime error: <text>` (exit 70), `// expect compile error: <text>` (exit 65), `// repl` (feed the file to the REPL), `// cflags: <flags>` (run on a sanitizer build with extra flags, e.g. `-DDEBUG_PRINT_CODE`), and `// stress-gc` (shorthand for `-DDEBUG_STRESS_GC`). Any sanitizer report fails the test.
```

- [ ] **Step 7: Commit**

```bash
git add tests CLAUDE.md
git commit -m "test: support per-test cflags and substring expectations, add check-all"
```

---

### Task 2: Fix the disassembler's local/upvalue operands

The disassembler prints local and upvalue operands by indexing the constant table with a stack-slot number. That reads out of bounds, or dereferences `NULL` when a function has no constants (`SEGV in special_inst`). Hot-loop analysis in later tasks depends on this tool.

**Files:**
- Modify: `src/debug.c` (add `slot_inst`, use it for 4 opcodes)
- Create: `tests/debug/disassemble_locals.bz`

**Interfaces:**
- Produces: the listing format `OpGetLocal          <slot>` (`%-16s %4d`, no constant value). Task 4 keeps this exact format.

- [ ] **Step 1: Write the failing test**

`tests/debug/disassemble_locals.bz`:

```
// cflags: -DDEBUG_PRINT_CODE
// Local and upvalue operands are slot numbers, not constant-table indices.
// This function has no constants at all, so indexing them used to crash.
fn f(a, b) {
  let c = a + b;
  return c;
}
print f(1, 2);
// expect contains: OpGetLocal          1
// expect contains: OpGetLocal          3
```

- [ ] **Step 2: Run it and confirm it fails**

Run: `tests/run.sh disassemble_locals`
Expected: `FAIL` with `sanitizer report` (`SEGV` in `special_inst`).

- [ ] **Step 3: Add `slot_inst` in `src/debug.c`**

Insert after `special_inst` (before `jmp_inst`):

```c
// Locals and upvalues are addressed by stack slot / upvalue index, not by
// constant-table index, so print the number only.
static uint32_t slot_inst(const char *name, const Chunk *chunk,
                          uint32_t offset) {
  uint32_t slot = 0;
  offset = read_idx(chunk, offset + 1, &slot);
  printf("%-16s %4d\n", name, slot);
  return offset;
}
```

In `disassemble_inst`, replace these four cases:

```c
  case OpGetUpvalue:
    return special_inst("OpGetUpvalue", chunk, offset, NULL);
  case OpSetUpvalue:
    return special_inst("OpSetUpvalue", chunk, offset, NULL);
  case OpGetLocal:
    return special_inst("OpGetLocal", chunk, offset, NULL);
  case OpSetLocal:
    return special_inst("OpSetLocal", chunk, offset, NULL);
```

with:

```c
  case OpGetUpvalue:
    return slot_inst("OpGetUpvalue", chunk, offset);
  case OpSetUpvalue:
    return slot_inst("OpSetUpvalue", chunk, offset);
  case OpGetLocal:
    return slot_inst("OpGetLocal", chunk, offset);
  case OpSetLocal:
    return slot_inst("OpSetLocal", chunk, offset);
```

- [ ] **Step 4: Run the test, then everything**

Run: `tests/run.sh disassemble_locals`
Expected: `passed: 1  failed: 0`

Run: `tests/check-all.sh`
Expected: `== all checks passed`

- [ ] **Step 5: Commit**

```bash
git add src/debug.c tests/debug
git commit -m "fix(debug): print local and upvalue operands as slot numbers

The disassembler looked up local/upvalue operands in the constant table,
reading out of bounds (or dereferencing NULL for functions without
constants) under DEBUG_PRINT_CODE."
```

---

### Task 3: Power-of-two masking in hash tables

`%` compiles to a hardware divide (20–40 cycles) on every probe. Capacities are always powers of two (`GROW_CAPACITY`: 8, 16, 32, …), so `hash & (capacity - 1)` gives the same index. This affects every global lookup (`fib` looks itself up on each call), every field access, and string interning.

**Files:**
- Modify: `src/table.c` (3 probe functions, 2 resize functions)
- Create: `tests/basics/many_globals.bz`, `tests/classes/many_fields.bz`

**Interfaces:**
- Consumes: `GROW_CAPACITY` from `src/memory.h` (must keep returning powers of two).
- Produces: the invariant that table and set capacities are powers of two, asserted in debug builds.

- [ ] **Step 1: Write the characterization tests (Review Focus 5)**

These pass before and after. They pin behaviour across many resizes while the indexing changes underneath.

Generate `tests/basics/many_globals.bz`:

```bash
python3 - <<'EOF'
lines = ["// 200 globals force several resizes of vm.globals."]
lines += [f"let g{i} = {i};" for i in range(1, 201)]
lines.append("let total = " + " + ".join(f"g{i}" for i in range(1, 201)) + ";")
lines.append("print total;   // expect: 20100")
lines.append("g200 = 0;")
lines.append("print g199 + g200;   // expect: 199")
open("tests/basics/many_globals.bz", "w").write("\n".join(lines) + "\n")
EOF
```

Generate `tests/classes/many_fields.bz`:

```bash
python3 - <<'EOF'
n = 40
lines = ["// 40 declared fields force several resizes of the class field set",
         "// and of each instance's field table."]
lines.append("class Wide {")
lines += [f"  let f{i};" for i in range(1, n + 1)]
lines.append("}")
lines.append("let w = Wide();")
lines += [f"w.f{i} = {i};" for i in range(1, n + 1)]
lines.append("print " + " + ".join(f"w.f{i}" for i in range(1, n + 1)) + ";   // expect: 820")
lines.append("print w.f40;   // expect: 40")
open("tests/classes/many_fields.bz", "w").write("\n".join(lines) + "\n")
EOF
```

- [ ] **Step 2: Run them against the current code**

Run: `tests/run.sh many_`
Expected: `passed: 2  failed: 0`. This is a refactor, so a failing test isn't possible; these guard the change.

- [ ] **Step 3: Switch probing to masks in `src/table.c`**

Add `#include <assert.h>` as the first include.

In `find_table_entry`, replace

```c
  uint32_t idx = key->hash % capacity;
```

with

```c
  // Capacities are powers of two, so masking equals `% capacity` without a
  // hardware divide.
  uint32_t mask = capacity - 1;
  uint32_t idx = key->hash & mask;
```

and replace `idx = (idx + 1) % capacity;` in the same function with `idx = (idx + 1) & mask;`.

In `table_find_string`, replace

```c
  uint32_t idx = hash % table->capacity;
```

with

```c
  uint32_t mask = table->capacity - 1;
  uint32_t idx = hash & mask;
```

and replace `idx = (idx + 1) % table->capacity;` with `idx = (idx + 1) & mask;`.

In `find_set_entry`, replace

```c
  uint32_t idx = key->hash % capacity;
```

with

```c
  uint32_t mask = capacity - 1;
  uint32_t idx = key->hash & mask;
```

and replace `idx = (idx + 1) % capacity;` with `idx = (idx + 1) & mask;`.

At the top of both `adjust_table_capacity` and `adjust_set_capacity`, add:

```c
  assert((capacity & (capacity - 1)) == 0 && "capacity must be a power of two");
```

- [ ] **Step 4: Verify nothing else uses modulo hashing**

Run: `grep -n "% capacity\|% table->capacity\|% set->capacity" src/table.c`
Expected: no output.

- [ ] **Step 5: Run everything**

Run: `tests/check-all.sh`
Expected: `== all checks passed`

- [ ] **Step 6: Measure**

Run: `python3 bench/run.py --langs breeze --runs 7 --compare build/bench/baseline.json --save build/bench/after-task3.json`
Expected: `fields`, `fib` and `strings` faster. No row below `0.97×`.

- [ ] **Step 7: Commit**

```bash
git add src/table.c tests/basics/many_globals.bz tests/classes/many_fields.bz
git commit -m "perf(table): index hash tables with a mask instead of modulo

Capacities are always powers of two, so hash & (capacity - 1) selects the
same bucket as hash % capacity without a hardware divide on every probe."
```

Put the `--compare` table from Step 6 in the commit body.

---

### Task 4: One-byte operands for locals and upvalues

`OpGetLocal 3` is encoded as `OpGetLocal OpConst 3`: three bytes and a width branch, on every local or upvalue access. Slots and upvalue indices are capped at 256, so a raw byte always fits. Globals keep the prefixed encoding because they index the constant table.

**Files:**
- Modify: `src/compiler.c` (`emit_variable_operation`, upvalue operands in `function`)
- Modify: `src/virtual_machine.c` (4 cases, `OpClosure` loop)
- Modify: `src/debug.c` (use `byte_inst`; remove `slot_inst`; closure loop)
- Modify: `CLAUDE.md` (bytecode encoding section)
- Create: `tests/debug/disassemble_slot_width.bz`, `tests/functions/closure_high_slot.bz`

**Interfaces:**
- Consumes: Task 2's listing format `%-16s %4d` (`byte_inst` prints the same format).
- Produces: encodings used by Tasks 5–8:
  - `OpGetLocal <u8 slot>`, `OpSetLocal <u8 slot>`, `OpGetUpvalue <u8 idx>`, `OpSetUpvalue <u8 idx>`
  - `OpClosure <OpConst|OpConstLong idx> { <u8 is_local> <u8 index> } × upvalues_len`
  - In `emit_variable_operation`: a local `bool is_global` that Task 7 relies on.

- [ ] **Step 1: Write the failing encoding test**

`tests/debug/disassemble_slot_width.bz`:

```
// cflags: -DDEBUG_PRINT_CODE
// OpGetLocal takes a single raw byte, so the OpRet after it starts at
// offset 2 (it was offset 3 with the OpConst width prefix).
fn f(a) {
  return a;
}
print f(7);
// expect contains: 0002     | OpRet
```

- [ ] **Step 2: Write the high-slot closure test (Review Focus 4)**

```bash
python3 - <<'EOF'
n = 210
lines = ["// Captures locals in slot 1 and slot 210: one-byte operands must not",
         "// truncate or sign-extend high slot numbers."]
lines.append("fn outer() {")
lines += [f"  let v{i} = {i};" for i in range(1, n + 1)]
lines.append(f"  fn inner() {{ return v1 + v{n}; }}")
lines.append(f"  v{n} = v{n} + 1000;")
lines.append("  return inner;")
lines.append("}")
lines.append("print outer()();   // expect: 1211")
open("tests/functions/closure_high_slot.bz", "w").write("\n".join(lines) + "\n")
EOF
```

- [ ] **Step 3: Run both tests and confirm the encoding test fails**

Run: `tests/run.sh slot`
Expected: `FAIL tests/debug/disassemble_slot_width.bz` (`stdout missing: 0002     | OpRet`). `closure_high_slot.bz` passes; it's a guard. Result: `passed: 1  failed: 1`.

- [ ] **Step 4: Emit one-byte operands in `src/compiler.c`**

Replace the whole of `emit_variable_operation` with:

```c
static void emit_variable_operation(const Token *name, bool can_assign) {
  uint8_t get_op, set_op;
  bool is_global = false;
  int32_t arg = resolve_local(current_compiler, name);
  if (arg != -1) {
    get_op = OpGetLocal;
    set_op = OpSetLocal;
  } else if ((arg = resolve_upvalue(current_compiler, name)) != -1) {
    get_op = OpGetUpvalue;
    set_op = OpSetUpvalue;
  } else {
    arg = emit_name(name);
    get_op = OpGetGlobal;
    set_op = OpSetGlobal;
    is_global = true;
  }

  if (can_assign && match_token(TokenEqual)) {
    expression();
    emit_byte(set_op);
  } else {
    emit_byte(get_op);
  }

  if (is_global) {
    // Globals index the constant table, which can exceed 255 entries.
    emit_idx(arg);
  } else {
    // Local slots and upvalue indices are capped at UINT8_COUNT, so a raw
    // byte always fits and saves the width prefix and its branch.
    emit_byte((uint8_t)arg);
  }
}
```

In `function()`, replace the upvalue loop:

```c
  for (uint32_t i = 0; i < func->upvalues_len; i += 1) {
    emit_byte(compiler.upvalues[i].is_local ? 1 : 0);
    emit_idx(compiler.upvalues[i].index);
  }
```

with:

```c
  for (uint32_t i = 0; i < func->upvalues_len; i += 1) {
    emit_byte(compiler.upvalues[i].is_local ? 1 : 0);
    emit_byte((uint8_t)compiler.upvalues[i].index);
  }
```

- [ ] **Step 5: Decode one-byte operands in `src/virtual_machine.c`**

Replace the four cases:

```c
    case OpSetLocal: {
      uint32_t local_stack_idx = READ_IDX(READ_BYTE());
      frame->frame_ptr[local_stack_idx] = peek_stack(0);
      break;
    }
    case OpGetLocal: {
      uint32_t local_stack_idx = READ_IDX(READ_BYTE());
      push_stack(frame->frame_ptr[local_stack_idx]);
      break;
    }
    case OpSetUpvalue: {
      uint32_t upvalue_idx = READ_IDX(READ_BYTE());
      *frame->closure->upvalues[upvalue_idx]->location = peek_stack(0);
      break;
    }
    case OpGetUpvalue: {
      uint32_t upvalue_idx = READ_IDX(READ_BYTE());
      push_stack(*frame->closure->upvalues[upvalue_idx]->location);
      break;
    }
```

with:

```c
    case OpSetLocal: {
      uint8_t slot = READ_BYTE();
      frame->frame_ptr[slot] = peek_stack(0);
      break;
    }
    case OpGetLocal: {
      uint8_t slot = READ_BYTE();
      push_stack(frame->frame_ptr[slot]);
      break;
    }
    case OpSetUpvalue: {
      uint8_t slot = READ_BYTE();
      *frame->closure->upvalues[slot]->location = peek_stack(0);
      break;
    }
    case OpGetUpvalue: {
      uint8_t slot = READ_BYTE();
      push_stack(*frame->closure->upvalues[slot]->location);
      break;
    }
```

In `case OpClosure`, replace `uint32_t index = READ_IDX(READ_BYTE());` with `uint8_t index = READ_BYTE();`.

- [ ] **Step 6: Disassemble one-byte operands in `src/debug.c`**

Replace the four `slot_inst` cases from Task 2 with:

```c
  case OpGetUpvalue:
    return byte_inst("OpGetUpvalue", chunk, offset);
  case OpSetUpvalue:
    return byte_inst("OpSetUpvalue", chunk, offset);
  case OpGetLocal:
    return byte_inst("OpGetLocal", chunk, offset);
  case OpSetLocal:
    return byte_inst("OpSetLocal", chunk, offset);
```

Delete the `slot_inst` function; it's now unused, and `-Werror` would reject it.

In `case OpClosure`, replace the loop:

```c
    for (uint32_t i = 0; i < function->upvalues_len; i += 1) {
      bool is_local = chunk->code[offset];
      offset = read_idx(chunk, offset + 1, &constant_idx);
      printf("%04d    |             %s %d\n", offset - 2,
             is_local ? "local" : "upvalue", constant_idx);
    }
```

with:

```c
    for (uint32_t i = 0; i < function->upvalues_len; i += 1) {
      bool is_local = chunk->code[offset];
      uint8_t index = chunk->code[offset + 1];
      printf("%04d    |             %s %d\n", offset,
             is_local ? "local" : "upvalue", index);
      offset += 2;
    }
```

- [ ] **Step 7: Run the tests, then everything**

Run: `tests/run.sh slot`
Expected: `passed: 2  failed: 0`

Run: `tests/check-all.sh`
Expected: `== all checks passed`. `disassemble_locals.bz` from Task 2 still passes because the listing format didn't change.

- [ ] **Step 8: Update `CLAUDE.md`**

In `## Bytecode encoding (non-obvious)`, replace the first bullet with:

```markdown
- **Index operands use `OpConst`/`OpConstLong` as a width prefix, except locals and upvalues.** Constant, global-name, property, class/method-name and closure-function operands are written via `emit_idx` → `write_constant_chunk` as `OpConst <u8>` or `OpConstLong <u24 little-endian>`, and the VM decodes them with `READ_IDX(READ_BYTE())` / `READ_STRING()`. Local slots and upvalue indices (≤ 255) are a single raw byte: `OpGetLocal <u8>`, and `OpClosure`'s upvalue pairs are `<u8 is_local> <u8 index>`. Any new opcode must use the same operand form in all three places: compiler, VM and `debug.c`.
```

- [ ] **Step 9: Measure**

Run: `python3 bench/run.py --langs breeze --runs 7 --compare build/bench/after-task3.json --save build/bench/after-task4.json`
Expected: `loop` and `closures` faster, since they do 7 and 4 slot accesses per iteration. No row below `0.97×`.

- [ ] **Step 10: Commit**

```bash
git add src/compiler.c src/virtual_machine.c src/debug.c CLAUDE.md tests/debug/disassemble_slot_width.bz tests/functions/closure_high_slot.bz
git commit -m "perf(compiler): encode local and upvalue operands as one raw byte

Slots and upvalue indices are capped at 256, so the OpConst width prefix
only cost an extra byte and a branch on every variable access."
```

Put the `--compare` table in the commit body.

---

### Task 5: Keep the instruction pointer in a local variable

Every `READ_BYTE()` currently goes through memory: it loads `frame->inst_ptr`, adds one, and stores it back. Keeping the pointer in a local (`ip`) lets gcc hold it in a register. The cost is discipline: `frame->inst_ptr` is only valid after `SAVE_IP()`, and two readers depend on it. `runtime_error()` walks every frame to print the trace, and `call_value()` pushes a frame whose caller must resume at the right place.

**Files:**
- Modify: `src/virtual_machine.c` (replace `run()`; delete `check_bool`, `read_byte`, `read_idx`, `read_string`)
- Create: `tests/errors/line_number_after_loop.bz`

**Interfaces:**
- Consumes: Task 4's one-byte slot encoding.
- Produces, for Tasks 6–8, these macros inside `run()`: `LOAD_FRAME()`, `SAVE_IP()`, `CODE()`, `READ_BYTE()`, `READ_WORD()`, `READ_IDX(width_op)`, `READ_VALUE(idx)`, `READ_CONSTANT(width_op)`, `READ_STRING()`, `RUNTIME_ERROR(...)` (saves `ip`, reports, returns `InterpretRuntimeErr`), and `BINARY_OP(value_type, op)`.

- [ ] **Step 1: Write the guard test (Review Focus 2)**

`tests/errors/line_number_after_loop.bz`:

```
fn check(n) {
  if (n == 3) { return n + "x"; }
  return n;
}

let i = 0;
while (i < 5) {
  check(i);
  i = i + 1;
}
// expect runtime error: Operands must be two numbers or two strings.
// expect runtime error: [line 2] in check()
// expect runtime error: [line 8] in script
```

- [ ] **Step 2: Run it against the current code**

Run: `tests/run.sh line_number_after_loop`
Expected: `passed: 1  failed: 0`. It passes today; if `ip` isn't saved before a call or error, the script frame would report a stale line.

- [ ] **Step 3: Delete the now-unused helpers in `src/virtual_machine.c`**

Delete these functions entirely: `check_bool`, `read_byte`, `read_idx`, `read_string`. Their logic moves into the macros below, and `-Werror` would reject them as unused.

- [ ] **Step 4: Replace `run()`**

Replace the whole `static InterpretResult run() { … }` function with:

```c
static InterpretResult run() {
  CallFrame *frame;
  uint8_t *ip;

// The instruction pointer lives in a local so gcc can keep it in a register.
// frame->inst_ptr is only current after SAVE_IP(), so call SAVE_IP() before
// anything that reads it: call_value() (the new frame's caller resumes from
// it) and runtime_error() (the trace walks every frame) -- RUNTIME_ERROR()
// does the latter for you.
#define LOAD_FRAME()                                                           \
  (frame = &vm.frames[vm.frames_len - 1], ip = frame->inst_ptr)
#define SAVE_IP() (frame->inst_ptr = ip)
#define CODE() (frame->closure->function->chunk.code)

#define READ_BYTE() (ip += 1, ip[-1])
#define READ_WORD() (ip += 2, (uint16_t)(ip[-2] | (ip[-1] << 8)))
// Index operand whose width is selected by the prefix opcode just read:
// OpConst -> 1 byte, OpConstLong -> 3 bytes little-endian. The long branch
// advances ip first and then reads fixed offsets, so no read is unsequenced
// with the increment.
#define READ_IDX(width_op)                                                     \
  ((width_op) == OpConst                                                       \
       ? (ip += 1, (uint32_t)ip[-1])                                           \
       : (ip += 3, (uint32_t)ip[-3] | ((uint32_t)ip[-2] << 8) |                \
                       ((uint32_t)ip[-1] << 16)))
#define READ_VALUE(idx) (frame->closure->function->chunk.constants.values[idx])
#define READ_CONSTANT(width_op) READ_VALUE(READ_IDX(width_op))
#define READ_STRING() AS_STRING(READ_CONSTANT(READ_BYTE()))

#define RUNTIME_ERROR(...)                                                     \
  do {                                                                         \
    SAVE_IP();                                                                 \
    runtime_error(__VA_ARGS__);                                                \
    return InterpretRuntimeErr;                                                \
  } while (false)

#define BINARY_OP(value_type, op)                                              \
  do {                                                                         \
    if (!IS_NUMBER(peek_stack(0)) || !IS_NUMBER(peek_stack(1))) {              \
      RUNTIME_ERROR("Operands must be numbers.");                              \
    }                                                                          \
    double right = AS_NUMBER(pop_stack());                                     \
    double left = AS_NUMBER(pop_stack());                                      \
    push_stack(value_type(left op right));                                     \
  } while (false)

  LOAD_FRAME();

  while (true) {
#ifdef DEBUG_TRACE_EXECUTION
    printf("        ");
    for (Value *stack_slot = vm.stack; stack_slot < vm.stack_ptr;
         stack_slot += 1) {
      printf("[ ");
      print_value(*stack_slot);
      printf(" ]");
    }
    printf("\n");
    disassemble_inst(&frame->closure->function->chunk,
                     (uint32_t)(ip - CODE()));
#endif /* DEBUG_TRACE_EXECUTION */
    uint8_t inst;
    switch (inst = READ_BYTE()) {
    case OpConst:
    case OpConstLong: {
      push_stack(READ_CONSTANT(inst));
      break;
    }
    case OpNull: {
      push_stack(NULL_VAL);
      break;
    }
    case OpTrue: {
      push_stack(BOOL_VAL(true));
      break;
    }
    case OpFalse: {
      push_stack(BOOL_VAL(false));
      break;
    }
    case OpDefineGlobal: {
      ObjString *name = READ_STRING();
      table_insert(&vm.globals, name, peek_stack(0));
      pop_stack();
      break;
    }
    case OpSetGlobal: {
      ObjString *name = READ_STRING();
      if (table_insert(&vm.globals, name, peek_stack(0))) {
        table_remove(&vm.globals, name);
        RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
      }
      break;
    }
    case OpGetGlobal: {
      ObjString *name = READ_STRING();
      Value value;
      if (!table_get(&vm.globals, name, &value)) {
        RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
      }
      push_stack(value);
      break;
    }
    case OpSetLocal: {
      uint8_t slot = READ_BYTE();
      frame->frame_ptr[slot] = peek_stack(0);
      break;
    }
    case OpGetLocal: {
      uint8_t slot = READ_BYTE();
      push_stack(frame->frame_ptr[slot]);
      break;
    }
    case OpSetUpvalue: {
      uint8_t slot = READ_BYTE();
      *frame->closure->upvalues[slot]->location = peek_stack(0);
      break;
    }
    case OpGetUpvalue: {
      uint8_t slot = READ_BYTE();
      push_stack(*frame->closure->upvalues[slot]->location);
      break;
    }
    case OpDefineProperty: {
      ObjClass *klass = AS_CLASS(peek_stack(0));
      ObjString *name = READ_STRING();
      if (set_contains(&klass->fields, name)) {
        RUNTIME_ERROR("Field %s is already defined.", name->chars);
      }
      set_insert(&klass->fields, name);
      break;
    }
    case OpSetProperty: {
      if (!IS_INSTANCE(peek_stack(1))) {
        RUNTIME_ERROR("Properties are defined for instances only.");
      }
      ObjInstance *instance = AS_INSTANCE(peek_stack(1));
      ObjString *name = READ_STRING();
      if (!set_contains(&instance->klass->fields, name)) {
        RUNTIME_ERROR("Undefined property '%s'.", name->chars);
      }
      table_insert(&instance->fields, name, peek_stack(0));
      Value value = pop_stack();
      pop_stack();
      push_stack(value);
      break;
    }
    case OpGetProperty: {
      if (!IS_INSTANCE(peek_stack(0))) {
        RUNTIME_ERROR("Properties are defined for instances only.");
      }
      ObjInstance *instance = AS_INSTANCE(peek_stack(0));
      ObjString *name = READ_STRING();
      Value value;
      if (!table_get(&instance->fields, name, &value)) {
        RUNTIME_ERROR("Undefined property '%s'", name->chars);
      }
      pop_stack();
      push_stack(value);
      break;
    }
    case OpEq: {
      Value right = pop_stack();
      Value left = pop_stack();
      push_stack(BOOL_VAL(values_equal(left, right)));
      break;
    }
    case OpLt: {
      BINARY_OP(BOOL_VAL, <);
      break;
    }
    case OpGt: {
      BINARY_OP(BOOL_VAL, >);
      break;
    }
    case OpAdd: {
      if (IS_STRING(peek_stack(0)) && IS_STRING(peek_stack(1))) {
        concat();
      } else if (IS_NUMBER(peek_stack(0)) && IS_NUMBER(peek_stack(1))) {
        double right = AS_NUMBER(pop_stack());
        double left = AS_NUMBER(pop_stack());
        push_stack(NUMBER_VAL(left + right));
      } else {
        RUNTIME_ERROR("Operands must be two numbers or two strings.");
      }
      break;
    }
    case OpSub: {
      BINARY_OP(NUMBER_VAL, -);
      break;
    }
    case OpMul: {
      BINARY_OP(NUMBER_VAL, *);
      break;
    }
    case OpDiv: {
      BINARY_OP(NUMBER_VAL, /);
      break;
    }
    case OpNeg: {
      if (!IS_NUMBER(peek_stack(0))) {
        RUNTIME_ERROR("Operand must be a number.");
      }
      push_stack(NUMBER_VAL(-AS_NUMBER(pop_stack())));
      break;
    }
    case OpNot: {
      if (!IS_BOOL(peek_stack(0))) {
        RUNTIME_ERROR("Operand must be a boolean.");
      }
      push_stack(BOOL_VAL(!AS_BOOL(pop_stack())));
      break;
    }
    case OpPrint: {
      print_value(pop_stack());
      printf("\n");
      break;
    }
    case OpPop: {
      pop_stack();
      break;
    }
    case OpJmpIfFalse: {
      uint16_t target = READ_WORD();
      if (!IS_BOOL(peek_stack(0))) {
        RUNTIME_ERROR("Operand must be a boolean.");
      }
      if (!AS_BOOL(peek_stack(0))) {
        ip = CODE() + target;
      }
      break;
    }
    case OpJmp: {
      // Read into a temporary: `ip = CODE() + READ_WORD()` would modify ip
      // twice without a sequence point.
      uint16_t target = READ_WORD();
      ip = CODE() + target;
      break;
    }
    case OpCall: {
      uint8_t args_len = READ_BYTE();
      SAVE_IP();
      if (!call_value(peek_stack(args_len), args_len)) {
        return InterpretRuntimeErr;
      }
      LOAD_FRAME();
      break;
    }
    case OpMethod: {
      define_method(READ_STRING());
      break;
    }
    case OpClosure: {
      ObjFunction *function = AS_FUNCTION(READ_CONSTANT(READ_BYTE()));
      ObjClosure *closure = new_closure(function);
      push_stack(OBJ_VAL(closure));
      for (uint32_t i = 0; i < closure->upvalues_len; i += 1) {
        uint8_t is_local = READ_BYTE();
        uint8_t index = READ_BYTE();
        if (is_local) {
          closure->upvalues[i] = capture_upvalue(frame->frame_ptr + index);
        } else {
          closure->upvalues[i] = frame->closure->upvalues[index];
        }
      }
      break;
    }
    case OpCloseUpvalue: {
      close_upvalues(vm.stack_ptr - 1);
      pop_stack();
      break;
    }
    case OpClass: {
      push_stack(OBJ_VAL(new_class(READ_STRING())));
      break;
    }
    case OpRet: {
      Value result = pop_stack();
      close_upvalues(frame->frame_ptr);
      vm.frames_len -= 1;
      if (vm.frames_len == 0) {
        pop_stack();
        return InterpretOk;
      }
      vm.stack_ptr = frame->frame_ptr;
      push_stack(result);
      LOAD_FRAME();
      break;
    }
    default: {
      RUNTIME_ERROR("Unknown opcode %d.", inst);
    }
    }
  }
#undef LOAD_FRAME
#undef SAVE_IP
#undef CODE
#undef READ_BYTE
#undef READ_WORD
#undef READ_IDX
#undef READ_VALUE
#undef READ_CONSTANT
#undef READ_STRING
#undef RUNTIME_ERROR
#undef BINARY_OP
}
```

- [ ] **Step 5: Run the guard tests, then everything**

Run: `tests/run.sh line_number`
Expected: `passed: 3  failed: 0` (`line_number`, `line_number_stack`, `line_number_after_loop`).

Run: `tests/check-all.sh`
Expected: `== all checks passed`

- [ ] **Step 6: Check the trace build still compiles**

Run: `gcc -std=c2x -Wall -Wextra -pedantic -Werror -DDEBUG_TRACE_EXECUTION -DDEBUG_PRINT_CODE -fsyntax-only -Isrc src/*.c`
Expected: no output.

- [ ] **Step 7: Measure**

Run: `python3 bench/run.py --langs breeze --runs 7 --compare build/bench/after-task4.json --save build/bench/after-task5.json`
Expected: every benchmark faster. No row below `0.97×`.

- [ ] **Step 8: Commit**

```bash
git add src/virtual_machine.c tests/errors/line_number_after_loop.bz
git commit -m "perf(vm): keep the instruction pointer in a local in run()

READ_BYTE() loaded and stored frame->inst_ptr on every byte. Cache it in
a local and write it back only where it is read: before call_value() and
inside RUNTIME_ERROR(), which also replaces check_bool()."
```

Put the `--compare` table in the commit body.

---

### Task 6: `OpJmpIfFalsePop` for statement conditions

`if`, `while` and `for` emit `OpJmpIfFalse` and then an `OpPop` on both paths. Since booleans are strict and the condition value is never reused, one instruction can test it, pop it, and jump. `&&` and `||` still need the non-popping `OpJmpIfFalse`, because the tested value is their result.

**Files:**
- Modify: `src/chunk.h` (new opcode)
- Modify: `src/compiler.c` (`if_statement`, `while_statement`, `for_statement`)
- Modify: `src/virtual_machine.c` (new case)
- Modify: `src/debug.c` (new case)
- Create: `tests/debug/disassemble_if_no_pop.bz`, `tests/errors/strict_bool_while.bz`

**Interfaces:**
- Consumes: Task 5's `READ_WORD()`, `CODE()`, `RUNTIME_ERROR()`.
- Produces: `OpJmpIfFalsePop <u16 absolute target>`. It pops the condition, raises `Operand must be a boolean.` if the condition isn't a bool, and jumps if it's `false`.

- [ ] **Step 1: Write the failing encoding test**

`tests/debug/disassemble_if_no_pop.bz`:

```
// cflags: -DDEBUG_PRINT_CODE
// Conditions of if/while/for are consumed by OpJmpIfFalsePop, so this
// program (no expression statements, no block locals) has no OpPop at all.
fn f(b) {
  if (b) { return 1; }
  return 2;
}
print f(true);
print f(false);
// expect contains: OpJmpIfFalsePop
// expect not contains: OpPop
```

The opcode name `OpJmpIfFalsePop` doesn't contain the substring `OpPop`, so the two expectations don't conflict.

- [ ] **Step 2: Write the strict-boolean guard (Review Focus 3)**

`tests/errors/strict_bool_while.bz`:

```
let n = 0;
while (n + 1) {   // expect runtime error: Operand must be a boolean.
  n = n + 1;
}
// expect runtime error: [line 2] in script
```

- [ ] **Step 3: Run them**

Run: `tests/run.sh disassemble_if_no_pop`
Expected: `FAIL` with `stdout missing: OpJmpIfFalsePop` and `stdout unexpectedly contains: OpPop`.

Run: `tests/run.sh strict_bool_while`
Expected: `passed: 1  failed: 0` (guard).

- [ ] **Step 4: Add the opcode to `src/chunk.h`**

In the `OpCode` enum, add `OpJmpIfFalsePop,` on the line after `OpJmpIfFalse,`.

- [ ] **Step 5: Emit it in `src/compiler.c`**

Replace `if_statement` with:

```c
static void if_statement() {
  expression();

  // OpJmpIfFalsePop consumes the condition on both paths, so neither branch
  // needs its own OpPop, and an `if` without `else` needs no OpJmp.
  uint32_t then_jmp = emit_jmp(OpJmpIfFalsePop);
  consume_token(TokenLeftBrace, "Expect '{' after 'if' statement.");
  scoped_block();

  if (match_token(TokenElse)) {
    uint32_t else_jmp = emit_jmp(OpJmp);
    patch_jmp(then_jmp);
    consume_token(TokenLeftBrace, "Expect '{' after 'else' statement.");
    scoped_block();
    patch_jmp(else_jmp);
  } else {
    patch_jmp(then_jmp);
  }
}
```

Replace `while_statement` with:

```c
static void while_statement() {
  uint32_t loop_start = current_chunk()->len;
  expression();

  uint32_t exit_jmp = emit_jmp(OpJmpIfFalsePop);
  consume_token(TokenLeftBrace, "Expect '{' after 'while' statement.");
  scoped_block();
  emit_loop(loop_start);

  patch_jmp(exit_jmp);
}
```

In `for_statement`, replace

```c
    exit_jmp = emit_jmp(OpJmpIfFalse);
    emit_byte(OpPop);
```

with

```c
    exit_jmp = emit_jmp(OpJmpIfFalsePop);
```

and replace

```c
  if (exit_jmp != -1) {
    patch_jmp(exit_jmp);
    emit_byte(OpPop);
  }
```

with

```c
  if (exit_jmp != -1) {
    patch_jmp(exit_jmp);
  }
```

- [ ] **Step 6: Execute it in `src/virtual_machine.c`**

Add this case directly after `case OpJmpIfFalse: { … }`:

```c
    case OpJmpIfFalsePop: {
      uint16_t target = READ_WORD();
      Value condition = pop_stack();
      if (!IS_BOOL(condition)) {
        RUNTIME_ERROR("Operand must be a boolean.");
      }
      if (!AS_BOOL(condition)) {
        ip = CODE() + target;
      }
      break;
    }
```

- [ ] **Step 7: Disassemble it in `src/debug.c`**

Add after `case OpJmpIfFalse:`'s `return` line:

```c
  case OpJmpIfFalsePop:
    return jmp_inst("OpJmpIfFalsePop", chunk, offset);
```

- [ ] **Step 8: Run the tests, then everything**

Run: `tests/run.sh disassemble_if_no_pop`
Expected: `passed: 1  failed: 0`

Run: `tests/check-all.sh`
Expected: `== all checks passed`. The `if/else`, `while`, `for`, `for_body_locals`, `logical` and closure-in-loop tests cover both branch shapes.

- [ ] **Step 9: Measure**

Run: `python3 bench/run.py --langs breeze --runs 7 --compare build/bench/after-task5.json --save build/bench/after-task6.json`
Expected: `loop`, `closures`, `fields` and `strings` faster (one fewer dispatch per iteration), and `fib` faster (its `if` lost an `OpPop` and an `OpJmp`). No row below `0.97×`.

- [ ] **Step 10: Commit**

```bash
git add src/chunk.h src/compiler.c src/virtual_machine.c src/debug.c tests/debug/disassemble_if_no_pop.bz tests/errors/strict_bool_while.bz
git commit -m "perf(compiler): test and pop statement conditions in one instruction

if/while/for emitted OpJmpIfFalse plus an OpPop on each path. The new
OpJmpIfFalsePop pops the condition itself; && and || keep OpJmpIfFalse
because the tested value is their result."
```

Put the `--compare` table in the commit body.

---

### Task 7: Fuse `x = expr;` into `OpSetLocalPop` / `OpSetUpvaluePop`

An assignment used as a statement emits `OpSetLocal slot` followed by `OpPop`. When an expression statement ends with a local or upvalue assignment, rewrite that set into a variant that pops instead of peeking.

**The trap (Review Focus 1):** a forward jump patched to land right after the set, such as the end of `a || (x = v)`, arrives expecting a value on the stack that the dropped `OpPop` would have removed. So `patch_jmp` must invalidate the fusion candidate.

**Files:**
- Modify: `src/chunk.h` (2 opcodes)
- Modify: `src/compiler.c` (`Compiler.last_set_offset`, `init_compiler`, `patch_jmp`, new `emit_statement_pop`, `emit_variable_operation`, `expression_statement`, `for_statement`)
- Modify: `src/virtual_machine.c` (2 cases)
- Modify: `src/debug.c` (2 cases)
- Create: `tests/debug/disassemble_set_pop.bz`, `tests/control/assign_short_circuit.bz`

**Interfaces:**
- Consumes: Task 4's `is_global` in `emit_variable_operation` and its one-byte slot encoding (a set instruction is exactly 2 bytes).
- Produces: `OpSetLocalPop <u8 slot>`, `OpSetUpvaluePop <u8 idx>`, and `static void emit_statement_pop()` in the compiler.

- [ ] **Step 1: Write the failing encoding test**

`tests/debug/disassemble_set_pop.bz`:

```
// cflags: -DDEBUG_PRINT_CODE
fn count(n) {
  let i = 0;
  while (i < n) {
    i = i + 1;
  }
  return i;
}
print count(3);
// expect contains: OpSetLocalPop
// expect not contains: OpPop
```

- [ ] **Step 2: Write the short-circuit guard (Review Focus 1)**

`tests/control/assign_short_circuit.bz`:

```
// The jump that skips the right-hand side lands exactly where the statement's
// pop would go. Fusing the assignment there would leave `true` on the stack
// and shift every later local by one slot.
fn f() {
  let x = false;
  true || (x = true);
  false && (x = true);
  let y = "y";
  print x;   // expect: false
  print y;   // expect: y
}
f();

fn g() {
  let z = 0;
  let hit = false;
  false || (hit = true);
  z = 5;
  print hit;   // expect: true
  print z;     // expect: 5
}
g();
```

- [ ] **Step 3: Run them**

Run: `tests/run.sh disassemble_set_pop`
Expected: `FAIL` with `stdout missing: OpSetLocalPop` and `stdout unexpectedly contains: OpPop`.

Run: `tests/run.sh assign_short_circuit`
Expected: `passed: 1  failed: 0` (guard; it must stay green after the change).

- [ ] **Step 4: Add the opcodes to `src/chunk.h`**

In the `OpCode` enum, add `OpSetUpvaluePop,` on the line after `OpSetUpvalue,` and `OpSetLocalPop,` on the line after `OpSetLocal,`.

- [ ] **Step 5: Track fusion candidates in `src/compiler.c`**

In `typedef struct Compiler`, add after `int32_t scope_depth;`:

```c
  // Offset of the most recent OpSetLocal/OpSetUpvalue, or -1. An expression
  // statement that ends with it can fuse the set and its OpPop.
  int32_t last_set_offset;
```

In `init_compiler`, after `compiler->scope_depth = 0;` add:

```c
  compiler->last_set_offset = -1;
```

Replace `patch_jmp` with:

```c
static void patch_jmp(uint32_t offset) {
  uint32_t jmp = current_chunk()->len;
  check_jmp_target(jmp);
  current_chunk()->code[offset] = jmp & 0xff;
  current_chunk()->code[offset + 1] = (jmp >> 8) & 0xff;
  // A jump now lands at the current end of the chunk. If a statement pop is
  // emitted here, the jumping path needs it too, so it must not be fused.
  current_compiler->last_set_offset = -1;
}

// Pops the value of an expression statement. If the expression ended with a
// local/upvalue assignment, turn that set into its popping variant instead of
// emitting a separate OpPop.
static void emit_statement_pop() {
  Chunk *chunk = current_chunk();
  int32_t set_offset = current_compiler->last_set_offset;
  current_compiler->last_set_offset = -1;
  if (set_offset >= 0 && (uint32_t)set_offset + 2 == chunk->len) {
    uint8_t *op = &chunk->code[set_offset];
    *op = (*op == OpSetLocal) ? OpSetLocalPop : OpSetUpvaluePop;
    return;
  }
  emit_byte(OpPop);
}
```

In `emit_variable_operation` (Task 4's version), replace

```c
  if (can_assign && match_token(TokenEqual)) {
    expression();
    emit_byte(set_op);
  } else {
```

with

```c
  if (can_assign && match_token(TokenEqual)) {
    expression();
    if (!is_global) {
      current_compiler->last_set_offset = (int32_t)current_chunk()->len;
    }
    emit_byte(set_op);
  } else {
```

Replace `expression_statement` with:

```c
static void expression_statement() {
  expression();
  consume_token(TokenSemiColon, "Expect ';' after value.");
  emit_statement_pop();
}
```

In `for_statement`'s increment clause, replace

```c
    expression();
    emit_byte(OpPop);
    consume_token(TokenRightParen, "Expect ')' after 'for' clauses.");
```

with

```c
    expression();
    emit_statement_pop();
    consume_token(TokenRightParen, "Expect ')' after 'for' clauses.");
```

- [ ] **Step 6: Execute them in `src/virtual_machine.c`**

Add after `case OpSetLocal: { … }`:

```c
    case OpSetLocalPop: {
      uint8_t slot = READ_BYTE();
      frame->frame_ptr[slot] = pop_stack();
      break;
    }
```

Add after `case OpSetUpvalue: { … }`:

```c
    case OpSetUpvaluePop: {
      uint8_t slot = READ_BYTE();
      *frame->closure->upvalues[slot]->location = pop_stack();
      break;
    }
```

- [ ] **Step 7: Disassemble them in `src/debug.c`**

Add next to the other slot cases:

```c
  case OpSetUpvaluePop:
    return byte_inst("OpSetUpvaluePop", chunk, offset);
  case OpSetLocalPop:
    return byte_inst("OpSetLocalPop", chunk, offset);
```

- [ ] **Step 8: Run the tests, then everything**

Run: `tests/run.sh assign`
Expected: `passed: 1  failed: 0`

Run: `tests/run.sh disassemble_set_pop`
Expected: `passed: 1  failed: 0`

Run: `tests/check-all.sh`
Expected: `== all checks passed`. `closure_shared` (`count = count + 1;` on an upvalue) and the `for` tests (increment clause) exercise both fused forms.

- [ ] **Step 9: Measure**

Run: `python3 bench/run.py --langs breeze --runs 7 --compare build/bench/after-task6.json --save build/bench/after-task7.json`
Expected: `loop` (2 fused statements per iteration), `closures`, `fields` and `strings` faster. No row below `0.97×`.

- [ ] **Step 10: Commit**

```bash
git add src/chunk.h src/compiler.c src/virtual_machine.c src/debug.c tests/debug/disassemble_set_pop.bz tests/control/assign_short_circuit.bz
git commit -m "perf(compiler): fuse local and upvalue assignment statements with their pop

'x = expr;' emitted a set followed by OpPop. Rewrite the trailing set into
OpSetLocalPop/OpSetUpvaluePop instead. patch_jmp cancels the fusion so a
short-circuit jump landing on the statement end still gets its pop."
```

Put the `--compare` table in the commit body.

---

### Task 8: Computed-goto dispatch with a switch fallback

A `switch` dispatches every instruction through one shared indirect branch, so the CPU's branch predictor has a single entry for all opcodes. With computed goto (`goto *table[op]`), each handler ends with its own indirect jump, and the predictor learns per-opcode successor patterns. This is typically 10–25% for this kind of loop. It's a GNU extension, so it sits behind `#if` with the `switch` as a fallback, and both modes are tested.

**Files:**
- Modify: `src/virtual_machine.c` (new `trace_execution`, replace `run()`)
- Modify: `tests/check-all.sh` (switch-dispatch run)
- Create: `tests/classes/method_declaration.bz`

**Interfaces:**
- Consumes: every opcode from Tasks 4, 6 and 7 and the macros from Task 5.
- Produces: `BREEZE_COMPUTED_GOTO` (1 on GCC/Clang unless `-DBREEZE_SWITCH_DISPATCH`), plus `CASE(op)`, `DISPATCH()`, `UNKNOWN_CASE` inside `run()`.

- [ ] **Step 1: Write the opcode-coverage guard**

Every opcode must have a dispatch-table entry, otherwise it falls into `Unknown opcode`. `OpMethod` is the only opcode no current test executes, so add `tests/classes/method_declaration.bz`:

```
class Greeter {
  let name;
  fn greet() { return 1; }
  fn wave(times) { return times; }
}
let g = Greeter();
g.name = "x";
print Greeter;   // expect: <class Greeter>
print g.name;    // expect: x
```

Run: `tests/run.sh method_declaration`
Expected: `passed: 1  failed: 0`

- [ ] **Step 2: Add the dispatch-mode switch and the trace helper in `src/virtual_machine.c`**

Insert directly above `static InterpretResult run()`:

```c
#if defined(__GNUC__) && !defined(BREEZE_SWITCH_DISPATCH)
#define BREEZE_COMPUTED_GOTO 1
#else
#define BREEZE_COMPUTED_GOTO 0
#endif

#ifdef DEBUG_TRACE_EXECUTION
static void trace_execution(CallFrame *frame, uint8_t *ip) {
  printf("        ");
  for (Value *stack_slot = vm.stack; stack_slot < vm.stack_ptr;
       stack_slot += 1) {
    printf("[ ");
    print_value(*stack_slot);
    printf(" ]");
  }
  printf("\n");
  Chunk *chunk = &frame->closure->function->chunk;
  disassemble_inst(chunk, (uint32_t)(ip - chunk->code));
}
#define TRACE() trace_execution(frame, ip)
#else
#define TRACE() ((void)0)
#endif

#if BREEZE_COMPUTED_GOTO
// Labels as values (`&&label`, `goto *ptr`) are a GNU extension, so silence
// -Wpedantic for the dispatch loop only. The table's range initializer is
// deliberately overridden per opcode, hence -Woverride-init.
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wpedantic"
#pragma GCC diagnostic ignored "-Woverride-init"
#endif
```

- [ ] **Step 3: Replace `run()`**

Replace the whole `static InterpretResult run() { … }` with this version. It keeps Task 5's macros and every case from Tasks 4–7, rewritten with `CASE()`/`DISPATCH()`.

```c
static InterpretResult run() {
  CallFrame *frame;
  uint8_t *ip;
  uint8_t inst;

// The instruction pointer lives in a local so gcc can keep it in a register.
// frame->inst_ptr is only current after SAVE_IP(), so call SAVE_IP() before
// anything that reads it: call_value() (the new frame's caller resumes from
// it) and runtime_error() (the trace walks every frame) -- RUNTIME_ERROR()
// does the latter for you.
#define LOAD_FRAME()                                                           \
  (frame = &vm.frames[vm.frames_len - 1], ip = frame->inst_ptr)
#define SAVE_IP() (frame->inst_ptr = ip)
#define CODE() (frame->closure->function->chunk.code)

#define READ_BYTE() (ip += 1, ip[-1])
#define READ_WORD() (ip += 2, (uint16_t)(ip[-2] | (ip[-1] << 8)))
// Index operand whose width is selected by the prefix opcode just read:
// OpConst -> 1 byte, OpConstLong -> 3 bytes little-endian. The long branch
// advances ip first and then reads fixed offsets, so no read is unsequenced
// with the increment.
#define READ_IDX(width_op)                                                     \
  ((width_op) == OpConst                                                       \
       ? (ip += 1, (uint32_t)ip[-1])                                           \
       : (ip += 3, (uint32_t)ip[-3] | ((uint32_t)ip[-2] << 8) |                \
                       ((uint32_t)ip[-1] << 16)))
#define READ_VALUE(idx) (frame->closure->function->chunk.constants.values[idx])
#define READ_CONSTANT(width_op) READ_VALUE(READ_IDX(width_op))
#define READ_STRING() AS_STRING(READ_CONSTANT(READ_BYTE()))

#define RUNTIME_ERROR(...)                                                     \
  do {                                                                         \
    SAVE_IP();                                                                 \
    runtime_error(__VA_ARGS__);                                                \
    return InterpretRuntimeErr;                                                \
  } while (false)

#define BINARY_OP(value_type, op)                                              \
  do {                                                                         \
    if (!IS_NUMBER(peek_stack(0)) || !IS_NUMBER(peek_stack(1))) {              \
      RUNTIME_ERROR("Operands must be numbers.");                              \
    }                                                                          \
    double right = AS_NUMBER(pop_stack());                                     \
    double left = AS_NUMBER(pop_stack());                                      \
    push_stack(value_type(left op right));                                     \
  } while (false)

#if BREEZE_COMPUTED_GOTO
  // One handler address per opcode; every unlisted byte is an unknown opcode.
  // A handler label without a table entry triggers -Wunused-label, and a
  // table entry without a label is a compile error, so the two lists cannot
  // silently drift apart.
  static void *dispatch_table[256] = {
      [0 ... 255] = &&op_unknown,
      [OpRet] = &&op_OpRet,
      [OpConst] = &&op_OpConst,
      [OpConstLong] = &&op_OpConstLong,
      [OpNull] = &&op_OpNull,
      [OpTrue] = &&op_OpTrue,
      [OpFalse] = &&op_OpFalse,
      [OpNot] = &&op_OpNot,
      [OpNeg] = &&op_OpNeg,
      [OpEq] = &&op_OpEq,
      [OpGt] = &&op_OpGt,
      [OpLt] = &&op_OpLt,
      [OpAdd] = &&op_OpAdd,
      [OpSub] = &&op_OpSub,
      [OpMul] = &&op_OpMul,
      [OpDiv] = &&op_OpDiv,
      [OpPrint] = &&op_OpPrint,
      [OpPop] = &&op_OpPop,
      [OpMethod] = &&op_OpMethod,
      [OpDefineProperty] = &&op_OpDefineProperty,
      [OpSetProperty] = &&op_OpSetProperty,
      [OpGetProperty] = &&op_OpGetProperty,
      [OpDefineGlobal] = &&op_OpDefineGlobal,
      [OpSetGlobal] = &&op_OpSetGlobal,
      [OpGetGlobal] = &&op_OpGetGlobal,
      [OpCloseUpvalue] = &&op_OpCloseUpvalue,
      [OpSetUpvalue] = &&op_OpSetUpvalue,
      [OpSetUpvaluePop] = &&op_OpSetUpvaluePop,
      [OpGetUpvalue] = &&op_OpGetUpvalue,
      [OpSetLocal] = &&op_OpSetLocal,
      [OpSetLocalPop] = &&op_OpSetLocalPop,
      [OpGetLocal] = &&op_OpGetLocal,
      [OpJmpIfFalse] = &&op_OpJmpIfFalse,
      [OpJmpIfFalsePop] = &&op_OpJmpIfFalsePop,
      [OpJmp] = &&op_OpJmp,
      [OpClosure] = &&op_OpClosure,
      [OpCall] = &&op_OpCall,
      [OpClass] = &&op_OpClass,
  };
#define CASE(op) op_##op:
#define DISPATCH()                                                             \
  do {                                                                         \
    TRACE();                                                                   \
    inst = READ_BYTE();                                                        \
    goto *dispatch_table[inst];                                                \
  } while (false)
#define UNKNOWN_CASE op_unknown:
#else
#define CASE(op) case op:
#define DISPATCH() break
#define UNKNOWN_CASE default:
#endif

  LOAD_FRAME();

#if BREEZE_COMPUTED_GOTO
  DISPATCH();
#else
  while (true) {
    TRACE();
    switch (inst = READ_BYTE()) {
#endif

    CASE(OpConst)
    CASE(OpConstLong) {
      push_stack(READ_CONSTANT(inst));
      DISPATCH();
    }
    CASE(OpNull) {
      push_stack(NULL_VAL);
      DISPATCH();
    }
    CASE(OpTrue) {
      push_stack(BOOL_VAL(true));
      DISPATCH();
    }
    CASE(OpFalse) {
      push_stack(BOOL_VAL(false));
      DISPATCH();
    }
    CASE(OpDefineGlobal) {
      ObjString *name = READ_STRING();
      table_insert(&vm.globals, name, peek_stack(0));
      pop_stack();
      DISPATCH();
    }
    CASE(OpSetGlobal) {
      ObjString *name = READ_STRING();
      if (table_insert(&vm.globals, name, peek_stack(0))) {
        table_remove(&vm.globals, name);
        RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
      }
      DISPATCH();
    }
    CASE(OpGetGlobal) {
      ObjString *name = READ_STRING();
      Value value;
      if (!table_get(&vm.globals, name, &value)) {
        RUNTIME_ERROR("Undefined variable '%s'.", name->chars);
      }
      push_stack(value);
      DISPATCH();
    }
    CASE(OpSetLocal) {
      uint8_t slot = READ_BYTE();
      frame->frame_ptr[slot] = peek_stack(0);
      DISPATCH();
    }
    CASE(OpSetLocalPop) {
      uint8_t slot = READ_BYTE();
      frame->frame_ptr[slot] = pop_stack();
      DISPATCH();
    }
    CASE(OpGetLocal) {
      uint8_t slot = READ_BYTE();
      push_stack(frame->frame_ptr[slot]);
      DISPATCH();
    }
    CASE(OpSetUpvalue) {
      uint8_t slot = READ_BYTE();
      *frame->closure->upvalues[slot]->location = peek_stack(0);
      DISPATCH();
    }
    CASE(OpSetUpvaluePop) {
      uint8_t slot = READ_BYTE();
      *frame->closure->upvalues[slot]->location = pop_stack();
      DISPATCH();
    }
    CASE(OpGetUpvalue) {
      uint8_t slot = READ_BYTE();
      push_stack(*frame->closure->upvalues[slot]->location);
      DISPATCH();
    }
    CASE(OpDefineProperty) {
      ObjClass *klass = AS_CLASS(peek_stack(0));
      ObjString *name = READ_STRING();
      if (set_contains(&klass->fields, name)) {
        RUNTIME_ERROR("Field %s is already defined.", name->chars);
      }
      set_insert(&klass->fields, name);
      DISPATCH();
    }
    CASE(OpSetProperty) {
      if (!IS_INSTANCE(peek_stack(1))) {
        RUNTIME_ERROR("Properties are defined for instances only.");
      }
      ObjInstance *instance = AS_INSTANCE(peek_stack(1));
      ObjString *name = READ_STRING();
      if (!set_contains(&instance->klass->fields, name)) {
        RUNTIME_ERROR("Undefined property '%s'.", name->chars);
      }
      table_insert(&instance->fields, name, peek_stack(0));
      Value value = pop_stack();
      pop_stack();
      push_stack(value);
      DISPATCH();
    }
    CASE(OpGetProperty) {
      if (!IS_INSTANCE(peek_stack(0))) {
        RUNTIME_ERROR("Properties are defined for instances only.");
      }
      ObjInstance *instance = AS_INSTANCE(peek_stack(0));
      ObjString *name = READ_STRING();
      Value value;
      if (!table_get(&instance->fields, name, &value)) {
        RUNTIME_ERROR("Undefined property '%s'", name->chars);
      }
      pop_stack();
      push_stack(value);
      DISPATCH();
    }
    CASE(OpEq) {
      Value right = pop_stack();
      Value left = pop_stack();
      push_stack(BOOL_VAL(values_equal(left, right)));
      DISPATCH();
    }
    CASE(OpLt) {
      BINARY_OP(BOOL_VAL, <);
      DISPATCH();
    }
    CASE(OpGt) {
      BINARY_OP(BOOL_VAL, >);
      DISPATCH();
    }
    CASE(OpAdd) {
      if (IS_STRING(peek_stack(0)) && IS_STRING(peek_stack(1))) {
        concat();
      } else if (IS_NUMBER(peek_stack(0)) && IS_NUMBER(peek_stack(1))) {
        double right = AS_NUMBER(pop_stack());
        double left = AS_NUMBER(pop_stack());
        push_stack(NUMBER_VAL(left + right));
      } else {
        RUNTIME_ERROR("Operands must be two numbers or two strings.");
      }
      DISPATCH();
    }
    CASE(OpSub) {
      BINARY_OP(NUMBER_VAL, -);
      DISPATCH();
    }
    CASE(OpMul) {
      BINARY_OP(NUMBER_VAL, *);
      DISPATCH();
    }
    CASE(OpDiv) {
      BINARY_OP(NUMBER_VAL, /);
      DISPATCH();
    }
    CASE(OpNeg) {
      if (!IS_NUMBER(peek_stack(0))) {
        RUNTIME_ERROR("Operand must be a number.");
      }
      push_stack(NUMBER_VAL(-AS_NUMBER(pop_stack())));
      DISPATCH();
    }
    CASE(OpNot) {
      if (!IS_BOOL(peek_stack(0))) {
        RUNTIME_ERROR("Operand must be a boolean.");
      }
      push_stack(BOOL_VAL(!AS_BOOL(pop_stack())));
      DISPATCH();
    }
    CASE(OpPrint) {
      print_value(pop_stack());
      printf("\n");
      DISPATCH();
    }
    CASE(OpPop) {
      pop_stack();
      DISPATCH();
    }
    CASE(OpJmpIfFalse) {
      uint16_t target = READ_WORD();
      if (!IS_BOOL(peek_stack(0))) {
        RUNTIME_ERROR("Operand must be a boolean.");
      }
      if (!AS_BOOL(peek_stack(0))) {
        ip = CODE() + target;
      }
      DISPATCH();
    }
    CASE(OpJmpIfFalsePop) {
      uint16_t target = READ_WORD();
      Value condition = pop_stack();
      if (!IS_BOOL(condition)) {
        RUNTIME_ERROR("Operand must be a boolean.");
      }
      if (!AS_BOOL(condition)) {
        ip = CODE() + target;
      }
      DISPATCH();
    }
    CASE(OpJmp) {
      // Read into a temporary: `ip = CODE() + READ_WORD()` would modify ip
      // twice without a sequence point.
      uint16_t target = READ_WORD();
      ip = CODE() + target;
      DISPATCH();
    }
    CASE(OpCall) {
      uint8_t args_len = READ_BYTE();
      SAVE_IP();
      if (!call_value(peek_stack(args_len), args_len)) {
        return InterpretRuntimeErr;
      }
      LOAD_FRAME();
      DISPATCH();
    }
    CASE(OpMethod) {
      define_method(READ_STRING());
      DISPATCH();
    }
    CASE(OpClosure) {
      ObjFunction *function = AS_FUNCTION(READ_CONSTANT(READ_BYTE()));
      ObjClosure *closure = new_closure(function);
      push_stack(OBJ_VAL(closure));
      for (uint32_t i = 0; i < closure->upvalues_len; i += 1) {
        uint8_t is_local = READ_BYTE();
        uint8_t index = READ_BYTE();
        if (is_local) {
          closure->upvalues[i] = capture_upvalue(frame->frame_ptr + index);
        } else {
          closure->upvalues[i] = frame->closure->upvalues[index];
        }
      }
      DISPATCH();
    }
    CASE(OpCloseUpvalue) {
      close_upvalues(vm.stack_ptr - 1);
      pop_stack();
      DISPATCH();
    }
    CASE(OpClass) {
      push_stack(OBJ_VAL(new_class(READ_STRING())));
      DISPATCH();
    }
    CASE(OpRet) {
      Value result = pop_stack();
      close_upvalues(frame->frame_ptr);
      vm.frames_len -= 1;
      if (vm.frames_len == 0) {
        pop_stack();
        return InterpretOk;
      }
      vm.stack_ptr = frame->frame_ptr;
      push_stack(result);
      LOAD_FRAME();
      DISPATCH();
    }
    UNKNOWN_CASE {
      RUNTIME_ERROR("Unknown opcode %d.", inst);
    }

#if !BREEZE_COMPUTED_GOTO
    }
  }
#endif

#undef LOAD_FRAME
#undef SAVE_IP
#undef CODE
#undef READ_BYTE
#undef READ_WORD
#undef READ_IDX
#undef READ_VALUE
#undef READ_CONSTANT
#undef READ_STRING
#undef RUNTIME_ERROR
#undef BINARY_OP
#undef CASE
#undef DISPATCH
#undef UNKNOWN_CASE
}

#if BREEZE_COMPUTED_GOTO
#pragma GCC diagnostic pop
#endif
#undef TRACE
```

- [ ] **Step 4: Test the switch fallback in `tests/check-all.sh`**

Add before the final `echo "== all checks passed"`:

```bash
echo "== switch dispatch (portable fallback)"
gcc -std=c2x -Wall -Wextra -pedantic -Werror -fsyntax-only \
  -DBREEZE_SWITCH_DISPATCH -I"$root/src" "$root"/src/*.c
CFLAGS_EXTRA=-DBREEZE_SWITCH_DISPATCH "$root/tests/run.sh"
```

- [ ] **Step 5: Run everything, including both trace builds**

Run: `tests/check-all.sh`
Expected: every section passes, including `== switch dispatch (portable fallback)`, then `== all checks passed`.

Run: `gcc -std=c2x -Wall -Wextra -pedantic -Werror -DDEBUG_TRACE_EXECUTION -DDEBUG_PRINT_CODE -fsyntax-only -Isrc src/*.c && gcc -std=c2x -Wall -Wextra -pedantic -Werror -DBREEZE_SWITCH_DISPATCH -DDEBUG_TRACE_EXECUTION -fsyntax-only -Isrc src/*.c`
Expected: no output.

- [ ] **Step 6: Measure**

Run: `python3 bench/run.py --langs breeze --runs 7 --compare build/bench/after-task7.json --save build/bench/after-task8.json`
Expected: every benchmark faster. Typical gains are 10–25% on `loop`/`closures`/`fib`. No row below `0.97×`.

- [ ] **Step 7: Commit**

```bash
git add src/virtual_machine.c tests/check-all.sh tests/classes/method_declaration.bz
git commit -m "perf(vm): dispatch with computed goto, keeping switch as a fallback

Each handler now ends in its own indirect jump through a 256-entry
table, so the branch predictor learns per-opcode successors. GCC/Clang
builds use it by default; -DBREEZE_SWITCH_DISPATCH selects the portable
switch, and check-all runs the suite in both modes."
```

Put the `--compare` table in the commit body.

---

### Task 9: Phase 1 report and pull request

**Files:**
- Modify: `docs/superpowers/specs/2026-10-06-breeze-performance-roadmap.md` (add a "Phase 1 results" section)
- Modify: `CLAUDE.md` (dispatch modes)

- [ ] **Step 1: Measure the whole phase**

Run: `python3 bench/run.py --langs breeze --runs 9 --compare build/bench/baseline.json`
Expected: geomean ≥ `1.40×`. If it's lower, record the actual number anyway; the report must state what happened.

Run: `python3 bench/run.py --runs 5`
Expected: the full cross-language table. The Breeze column should beat `python 3` on every row.

- [ ] **Step 2: Record the results in the roadmap**

Append to `docs/superpowers/specs/2026-10-06-breeze-performance-roadmap.md`:

```markdown
## Phase 1 results (<date of the run>)

Per-task speedups (from each task's `--compare` table):

| task | change | geomean speedup |
|---|---|---|
| 3 | hash mask | <value>× |
| 4 | one-byte slot operands | <value>× |
| 5 | register instruction pointer | <value>× |
| 6 | OpJmpIfFalsePop | <value>× |
| 7 | fused assignment statements | <value>× |
| 8 | computed goto | <value>× |
| | **Phase 1 total vs baseline** | **<value>×** |

<paste the full cross-language table from Step 1>

Instructions per `loop` iteration: 16 before → <count from DEBUG_PRINT_CODE> after.
```

Replace each `<…>` with the measured value from Step 1 and the saved `build/bench/after-task*.json` comparisons. This is a report of measurements, not a placeholder in the plan.

- [ ] **Step 3: Document dispatch in `CLAUDE.md`**

Append to the `## Pipeline` section:

```markdown
- `run()` dispatches with computed goto (`dispatch_table`, `CASE()`/`DISPATCH()` macros) on GCC/Clang. `-DBREEZE_SWITCH_DISPATCH` selects a portable `switch`, and `tests/check-all.sh` tests both. A new opcode needs a `CASE()` handler, a `dispatch_table` entry (a missing one shows up as `-Wunused-label` or a compile error), compiler emission, and a `debug.c` case.
- The instruction pointer is cached in the local `ip` inside `run()`. `frame->inst_ptr` is only current after `SAVE_IP()`, which must happen before `call_value()` and on every error path (use `RUNTIME_ERROR(...)`).
```

- [ ] **Step 4: Run everything one last time**

Run: `tests/check-all.sh`
Expected: `== all checks passed`

- [ ] **Step 5: Commit, push, open the PR**

```bash
git add docs/superpowers/specs/2026-10-06-breeze-performance-roadmap.md CLAUDE.md
git commit -m "docs(perf): record Phase 1 interpreter fast-path results"
git push -u origin perf/benchmarks
gh pr create --base main --head perf/benchmarks \
  --title "perf: interpreter fast path (phase 1)" \
  --body-file <(sed -n '/## Phase 1 results/,$p' docs/superpowers/specs/2026-10-06-breeze-performance-roadmap.md)
```

The PR description is the results section, with no "Generated with" line.
