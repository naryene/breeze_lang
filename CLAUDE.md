# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Breeze is a dynamically typed scripting language implemented in C (C23) as a single-pass bytecode compiler + stack VM. Its architecture closely follows clox from *Crafting Interpreters*, so that book is the best reference for intent — but several semantics deliberately diverge (see below).

## Build & run

```sh
bash run.sh                  # cmake configure+build into build/, then runs build/breeze test.txt
build/breeze <file>          # run a script
build/breeze                 # REPL (each line is compiled independently)
bash debug.sh                # gdb --args build/breeze test.txt
```

- Default build type is `Debug`, which enables ASan + UBSan.
- Without cmake: `gcc -std=c2x -Wall -Wextra -g -fsanitize=address,undefined -Isrc src/*.c -o breeze`
- Exit codes: 64 usage, 65 compile error, 70 runtime error, 74 file I/O.
- Tests: `tests/check-all.sh` is the gate for every change. It checks warnings with `-Werror` and runs the suite under ASan/UBSan, under `DEBUG_STRESS_GC`, and against an `-O2` build. `tests/run.sh [substring]` runs the suite (or a subset) once. Expectations live in comments: `// expect: <stdout line>`, `// expect contains: <text>` / `// expect not contains: <text>` (substring checks, which disable line-by-line comparison), `// expect runtime error: <text>` (exit 70), `// expect compile error: <text>` (exit 65), `// repl` (feed the file to the REPL), `// cflags: <flags>` (run on a sanitizer build with extra flags, e.g. `-DDEBUG_PRINT_CODE`), and `// stress-gc` (shorthand for `-DDEBUG_STRESS_GC`). Any sanitizer report fails the test.
- Debug toggles are `#define`s in `src/common.h`: `DEBUG_PRINT_CODE` (disassemble after compile), `DEBUG_TRACE_EXECUTION` (stack + instruction trace), `DEBUG_STRESS_GC` (collect on every allocation), `DEBUG_LOG_GC`.

## Benchmarks

- `python3 bench/run.py` times `bench/<name>.{bz,ts,lua,py,rs}` on every installed runtime (Breeze -O2, Lua, LuaJIT, Python, Node/Bun/Deno running TypeScript, Rust -O3), checks each program's output, and prints medians.
- `--langs breeze --save build/bench/x.json` records a run, and `--compare build/bench/x.json` prints per-benchmark speedups plus the geometric mean. Use these around every performance change.
- `bench/ab.py <rev-a> [<rev-b>]` builds two revisions (default B: the working tree) and alternates their runs, so both see the same machine state; use it to gate performance changes. Both tools link `virtual_machine.c` first and build with `-falign-jumps=32 -falign-labels=32 -falign-loops=32`, because code-layout shifts alone move timings by ~10%; with both, A/A runs agree within ±0.2%. `--cflags` adds flags to both sides.
- Benchmark programs may only use features Breeze has: no arrays, no `%`, no number→string conversion.
- Performance roadmap: `docs/superpowers/specs/2026-10-06-breeze-performance-roadmap.md`.

## Pipeline

`main.c` → `interpret()` (virtual_machine.c) → `compile()` (compiler.c pulls tokens on demand from scanner.c) → returns top-level `ObjFunction` → wrapped in `ObjClosure`, called as frame 0 → `run()` dispatch loop.

There is no AST: the Pratt parser (`rules[]` table in compiler.c) emits bytecode directly into `current_compiler->function->chunk`. Nested functions push a new `Compiler` linked via `enclosing`.

- `run()` dispatches with computed goto (`dispatch_table`, `CASE()`/`DISPATCH()` macros) on GCC/Clang. `-DBREEZE_SWITCH_DISPATCH` selects a portable `switch`, and `tests/check-all.sh` tests both. A new opcode needs a `CASE()` handler, a `dispatch_table` entry (a missing one shows up as `-Wunused-label` or a compile error), compiler emission, and a `debug.c` case.
- The instruction pointer is cached in the local `ip` inside `run()`. `frame->inst_ptr` is only current after `SAVE_IP()`, which must happen before `call_value()` and on every error path (use `RUNTIME_ERROR(...)`).

## Bytecode encoding (non-obvious)

- **Index operands use `OpConst`/`OpConstLong` as a width prefix, except locals and upvalues.** Constant, global-name, property, class/method-name and closure-function operands are written via `emit_idx` → `write_constant_chunk` as `OpConst <u8>` or `OpConstLong <u24 little-endian>`, and the VM decodes them with `READ_IDX(READ_BYTE())` / `READ_STRING()`. Local slots and upvalue indices (≤ 255) are a single raw byte: `OpGetLocal <u8>`, and `OpClosure`'s upvalue pairs are `<u8 is_local> <u8 index>`. Any new opcode must use the same operand form in all three places: compiler, VM and `debug.c`.
- **Jumps are absolute** 16-bit little-endian targets into the chunk (`READ_WORD`), not relative offsets. `patch_jmp` writes the current chunk length; `emit_loop` writes `loop_start`.
- Line info is a run-length `LineVec` of `{line, last_offset}` pairs, queried by binary search in `get_line`.

## Language semantics that differ from Lox

- Keywords: `let`, `fn`, `class`, `print`, `&&`, `||`, `null`, `self` (reserved, not wired), `super` (reserved), `impl` (token only).
- **Strict booleans:** `!`, `if`, `while`, `for`, `&&`, `||` require `bool` operands — there is no truthiness; non-bools are a runtime error.
- **Declared fields:** class bodies list fields as `let name;` before methods (`fn name() {}`). `OpDefineProperty` records them in the class's `Set fields`; `OpSetProperty` rejects undeclared names, and `OpGetProperty` errors on a declared-but-unset field.
- Methods are compiled and stored in `klass->methods` via `OpMethod`, but method lookup/invocation and `self` binding are **not implemented yet** (work in progress).

## Memory / GC

- All heap allocation goes through `reallocate()` (memory.c), which can trigger `collect_garbage()`. Any freshly allocated object not yet reachable from a root must be protected by pushing it on the VM stack around further allocations (see `allocate_string`, `add_constant`, `define_native`). Test GC-safety with `DEBUG_STRESS_GC`.
- Roots: VM stack, call-frame closures, open upvalues, `vm.globals`, and every in-progress `Compiler` function (`mark_compiler_roots`). `vm.strings` is the interning table and is weak (`table_remove_white` before sweep).
- Every new `Obj` type needs cases in `blacken_object`, `free_object` (memory.c), `print_object` (object.c), plus `IS_*`/`AS_*` macros in object.h.
- Hash tables use tombstones: in `Table`, an empty slot is `key == NULL && value == NULL_VAL`; a tombstone is `key == NULL && value == true`. `Set` uses an explicit `is_tombstone` flag.

## Conventions

- Naming: `snake_case` functions, `PascalCase` types and enum variants (`OpAdd`, `TokenLet`, `ObjStringType`, `ValNumber`), header guards `breeze_<name>_h`.
- Counters are `uint32_t` with `+= 1` (no `++`); single global `vm`, `parser`, `scanner`, `current_compiler`.
