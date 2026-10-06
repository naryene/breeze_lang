# Breeze Performance Roadmap

**Date:** 2026-10-06
**Goal:** make Breeze as fast as possible, with Rust-level performance as the long-term aim.

## Where we are (2026-10-06, `bench/run.py`, Ryzen 9 8940HX)

Median wall-clock time; ratio = time ÷ Breeze time (lower is faster).

| benchmark | breeze | lua 5.5 | python 3.14 | luajit | bun | rust -O3 |
|---|---|---|---|---|---|---|
| fib(35) | 1.84 s | 0.39× | 0.79× | 0.06× | 0.05× | 0.04× |
| loop 20M | 1.18 s | 0.19× | 1.45× | 0.02× | 0.03× | 0.02× |
| closures 15M | 1.31 s | 0.38× | 1.16× | 0.02× | 0.01× | 0.02× |
| fields 10M | 1.27 s | 0.33× | 0.82× | 0.01× | 0.03× | 0.01× |
| strings 8M | 1.10 s | 0.44× | 0.94× | 0.01× | 0.01× | 0.26× |

Compiler flags are not the bottleneck: `-O3 -march=native -flto` is within ±5% of `-O2`.
The cost is structural. The `loop` body executes 16 instructions (36 bytes) per
iteration, against roughly 5 for Lua's register VM.

## The honest ceiling of each approach

| Approach | Realistic speed vs Rust on these benchmarks | Reference |
|---|---|---|
| Tuned stack interpreter (Phases 1–3) | 10–30× slower | Lua 5.5, CPython 3.14 |
| Register interpreter (Phase 4) | 8–20× slower | Lua 5.5 |
| Specializing JIT for dynamic code (Phase 5a) | 1–3× slower | LuaJIT, V8, JavaScriptCore |
| Optional static types + AOT native code (Phase 5b) | 1–2× slower on typed code | Crystal, Nim, Cython |

**No interpreter reaches Rust.** Rust-level speed needs native code specialized
to the actual types. For a dynamically typed language there are two ways to get
it: (a) observe types at runtime and JIT-compile specialized machine code, the
LuaJIT/V8 route, which takes years to do well; or (b) let programs declare types
and compile those parts ahead of time, the Crystal/Cython route. Route (b) is the
realistic way for one person to get Rust-like numbers, because a mature backend
(C via gcc, or LLVM/Cranelift) does the heavy optimization.

## Phases

Each phase is its own plan and must leave Breeze shippable: all tests green under
ASan/UBSan and `DEBUG_STRESS_GC`, and no benchmark regression.

### Phase 1: Interpreter fast path
Plan: `docs/superpowers/plans/2026-10-06-breeze-interpreter-fast-path.md`

- Benchmark baseline and before/after comparison tooling.
- Test-runner support for per-test compiler flags and substring expectations.
- Fix the disassembler, which is the measurement tool.
- Power-of-two masking instead of `%` in hash tables.
- One-byte operands for local and upvalue instructions.
- Keep the instruction pointer in a local variable inside `run()`.
- New `OpJmpIfFalsePop` for `if`/`while`/`for` conditions.
- Fuse `x = expr;` statements into `OpSetLocalPop` / `OpSetUpvaluePop`.
- Computed-goto dispatch with a portable `switch` fallback.

**Target:** at least 1.4× geometric-mean speedup over the baseline, and faster
than CPython on every benchmark.

### Phase 2: Object model
- Instances store fields in fixed slots. Fields are declared per class, so
  `p.x` resolves to a slot index; we can cache it per call site.
- Globals in an indexed array instead of a hash table. The name→index map lives
  in the VM so it works with the REPL.
- Method invocation (feature work), designed with inline caches from the start.

**Target:** `fields` and `fib` within 1.5× of Lua 5.5.

### Phase 3: Values and the stack
- NaN-boxing: `Value` goes from 16 to 8 bytes.
- Keep the stack pointer in a local variable in `run()` (it must be saved before
  anything that can allocate, because the GC scans the stack).
- Compute each function's maximum stack depth at compile time and check it once
  per call instead of on every push.

### Phase 4: Register VM
- Lua-5-style register instructions (`ADD r1 r2 r3`) cut the instruction count
  by about 3× on arithmetic loops. This rewrites the compiler back end.

**Target:** parity with Lua 5.5.

### Phase 5: Native code (the Rust-performance phase)
- **5b first (recommended): optional type annotations plus AOT compilation of
  typed functions to C**, compiled by gcc and linked with the VM. Untyped code
  keeps running in the interpreter.
- **5a later (optional):** a baseline JIT (copy-and-patch or direct x86-64
  emission) with type guards for untyped hot loops.

**Target:** typed `fib`/`loop`/`fields` within 2× of Rust.

## Methodology (applies to every phase)
1. Change one thing per commit. Run
   `bench/run.py --langs breeze --compare <previous.json>` before and after.
2. Keep a change only if the full test suite passes (normal, `DEBUG_STRESS_GC`,
   `-O2` build) and no benchmark regresses by more than 3%.
3. Before measuring, write down which benchmark should move and why.
4. Count bytecode instructions per hot-loop iteration with `DEBUG_PRINT_CODE`
   alongside wall-clock time.
