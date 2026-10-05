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
- There is no test suite. `test.txt` is the ad-hoc smoke script; verify changes by writing small `.bz`-style scripts and running them.
- Debug toggles are `#define`s in `src/common.h`: `DEBUG_PRINT_CODE` (disassemble after compile), `DEBUG_TRACE_EXECUTION` (stack + instruction trace), `DEBUG_STRESS_GC` (collect on every allocation), `DEBUG_LOG_GC`.

## Pipeline

`main.c` → `interpret()` (virtual_machine.c) → `compile()` (compiler.c pulls tokens on demand from scanner.c) → returns top-level `ObjFunction` → wrapped in `ObjClosure`, called as frame 0 → `run()` dispatch loop.

There is no AST: the Pratt parser (`rules[]` table in compiler.c) emits bytecode directly into `current_compiler->function->chunk`. Nested functions push a new `Compiler` linked via `enclosing`.

## Bytecode encoding (non-obvious)

- **Variable-width operands reuse `OpConst`/`OpConstLong` as a width prefix.** Any operand that is an index (constant, global name, local slot, upvalue slot, property name, class/method name, closure upvalue index) is written via `emit_idx` → `write_constant_chunk`, producing either `OpConst <u8>` or `OpConstLong <u24 little-endian>`. So `OpGetLocal 3` is actually encoded as `OpGetLocal OpConst 3`. The VM decodes with `READ_IDX(READ_BYTE())` / `READ_STRING()`; the disassembler uses `special_inst` / `read_idx`. Any new opcode with an index operand must follow this convention in all three places (compiler, VM, debug.c).
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
