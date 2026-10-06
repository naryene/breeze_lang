#!/usr/bin/env python3
"""Cross-language benchmark runner for Breeze.

Runs bench/<name>.{bz,ts,lua,py,rs} on every available runtime, checks each
program's output against the expected checksum, and prints a Markdown table
of median wall-clock times (process startup included).

Usage: bench/run.py [--runs N] [--only fib,loop] [--langs breeze,lua]
"""

import argparse
import json
import os
import shutil
import statistics
import subprocess
import sys
import time
from pathlib import Path

BENCH = Path(__file__).resolve().parent
ROOT = BENCH.parent
BUILD = ROOT / "build" / "bench"

# Expected results. Breeze prints numbers with %g (6 significant digits), so
# outputs are compared numerically with a relative tolerance.
BENCHMARKS = {
    "startup": 0,  # empty program: process + runtime startup cost only
    "fib": 9227465,
    "loop": 199999990000000,
    "closures": 15000000,
    "fields": 50000005000000,
    "strings": 8000000,
}


def find(tool, *extra_paths):
    found = shutil.which(tool)
    if found:
        return found
    for path in extra_paths:
        candidate = Path(path).expanduser()
        if candidate.exists():
            return str(candidate)
    return None


def build_breeze():
    out = BUILD / "breeze-release"
    # virtual_machine.c first: otherwise any size change in a file linked
    # before it moves the dispatch loop and shifts timings by ~10%.
    files = sorted((ROOT / "src").glob("*.c"))
    sources = [str(f) for f in files if f.name == "virtual_machine.c"]
    sources += [str(f) for f in files if f.name != "virtual_machine.c"]
    subprocess.run(
        # Fixed branch-target alignment: see ALIGN in bench/ab.py.
        ["gcc", "-std=c2x", "-O2", "-DNDEBUG", "-falign-jumps=32",
         "-falign-labels=32", "-falign-loops=32", f"-I{ROOT / 'src'}", *sources,
         "-o", str(out)],
        check=True,
    )
    return out


def build_rust(names):
    rustc = find("rustc")
    if not rustc:
        return None
    for name in names:
        subprocess.run(
            [rustc, "-C", "opt-level=3", "-C", "target-cpu=native",
             str(BENCH / f"{name}.rs"), "-o", str(BUILD / f"{name}-rs")],
            check=True,
        )
    return True


def languages(breeze_bin, have_rust):
    """(label, file extension, argv builder) for each available runtime."""
    langs = [("breeze", "bz", lambda f: [str(breeze_bin), str(f)])]

    def add(label, ext, tool, argv, *extra_paths):
        path = find(tool, *extra_paths)
        if path:
            langs.append((label, ext, lambda f, p=path: argv(p, f)))

    add("lua 5.5", "lua", "lua", lambda p, f: [p, str(f)])
    add("luajit", "lua", "luajit", lambda p, f: [p, str(f)])
    add("python 3", "py", "python3", lambda p, f: [p, str(f)])
    add("node (ts)", "ts", "node", lambda p, f: [p, "--no-warnings", str(f)])
    add("bun (ts)", "ts", "bun", lambda p, f: [p, "run", str(f)])
    add("deno (ts)", "ts", "deno", lambda p, f: [p, "run", "--quiet", str(f)],
        "~/.deno/bin/deno")
    if have_rust:
        langs.append(("rust -O3", "rs",
                      lambda f: [str(BUILD / f"{Path(f).stem}-rs")]))
    return langs


def run_once(argv):
    start = time.perf_counter()
    proc = subprocess.run(argv, capture_output=True, text=True)
    elapsed = time.perf_counter() - start
    return elapsed, proc


def check_output(proc, expected):
    if proc.returncode != 0:
        return f"exit {proc.returncode}: {proc.stderr.strip()[:80]}"
    try:
        value = float(proc.stdout.strip().splitlines()[-1])
    except (ValueError, IndexError):
        return f"bad output: {proc.stdout.strip()[:80]!r}"
    if abs(value - expected) > abs(expected) * 1e-5:
        return f"wrong result {value} (expected {expected})"
    return None


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--runs", type=int, default=5)
    parser.add_argument("--only", default="")
    parser.add_argument("--langs", default="")
    parser.add_argument("--save", default="",
                        help="write results as JSON to this path")
    parser.add_argument("--compare", default="",
                        help="JSON from an earlier --save; prints Breeze speedups")
    args = parser.parse_args()

    names = [n for n in BENCHMARKS if not args.only or n in args.only.split(",")]
    BUILD.mkdir(parents=True, exist_ok=True)

    print("building breeze (-O2) and rust (-O3)...", file=sys.stderr)
    breeze_bin = build_breeze()
    have_rust = build_rust(names)

    langs = languages(breeze_bin, have_rust)
    if args.langs:
        wanted = args.langs.split(",")
        langs = [l for l in langs if any(w in l[0] for w in wanted)]

    results = {}  # (bench, lang) -> median seconds or error string
    for name in names:
        for label, ext, argv in langs:
            source = BENCH / f"{name}.{ext}"
            if not source.exists():
                continue
            cmd = argv(source)
            _, proc = run_once(cmd)  # warm-up (page cache, JIT caches)
            error = check_output(proc, BENCHMARKS[name])
            if error:
                results[(name, label)] = error
                print(f"  {name:9} {label:10} FAILED: {error}", file=sys.stderr)
                continue
            times = [run_once(cmd)[0] for _ in range(args.runs)]
            results[(name, label)] = statistics.median(times)
            print(f"  {name:9} {label:10} {results[(name, label)]:.3f}s",
                  file=sys.stderr)

    labels = [l[0] for l in langs]
    print()
    print(f"Median of {args.runs} runs, wall clock incl. process startup. "
          "Ratio = time / breeze time (lower is faster).")
    print()
    print("| benchmark | " + " | ".join(labels) + " |")
    print("|---" * (len(labels) + 1) + "|")
    for name in names:
        base = results.get((name, "breeze"))
        cells = []
        for label in labels:
            r = results.get((name, label))
            if r is None:
                cells.append("—")
            elif isinstance(r, str):
                cells.append("FAIL")
            elif label == "breeze" or not isinstance(base, float):
                cells.append(f"{r:.3f}s")
            else:
                cells.append(f"{r:.3f}s ({r / base:.2f}×)")
        print(f"| {name} | " + " | ".join(cells) + " |")

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


if __name__ == "__main__":
    main()
