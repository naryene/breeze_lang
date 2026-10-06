#!/usr/bin/env python3
"""Interleaved A/B benchmark of two Breeze revisions.

Usage: bench/ab.py <rev-a> [<rev-b>] [--runs N] [--only fib,loop]
       rev-b defaults to the working tree (spelled WORKTREE).

Both sides are built the same way as bench/run.py (-O2, virtual_machine.c
linked first) and their runs alternate A, B, A, B, ... so both see the same
machine state (CPU frequency, thermals, page cache). Prints per-benchmark
medians and the speedup A/B (> 1 means B is faster).
"""

import argparse
import statistics
import subprocess
import sys
import tempfile
import time
from pathlib import Path

BENCH = Path(__file__).resolve().parent
ROOT = BENCH.parent
NAMES = ["fib", "loop", "closures", "fields", "strings"]


def link_order(src_dir):
    """virtual_machine.c first, so the dispatch loop's address does not move
    when an unrelated file changes size (that alone shifts timings by ~10%)."""
    files = sorted(Path(src_dir).glob("*.c"))
    vm = [f for f in files if f.name == "virtual_machine.c"]
    return [str(f) for f in vm + [f for f in files if f.name != "virtual_machine.c"]]


def build(rev, out, workdir):
    if rev == "WORKTREE":
        src = ROOT / "src"
    else:
        dest = Path(workdir) / f"src-{Path(out).name}"
        dest.mkdir(parents=True)
        archive = subprocess.run(["git", "-C", str(ROOT), "archive", rev, "src"],
                                 check=True, capture_output=True).stdout
        subprocess.run(["tar", "-x", "-C", str(dest)], input=archive, check=True)
        src = dest / "src"
    subprocess.run(["gcc", "-std=c2x", "-O2", "-DNDEBUG", f"-I{src}",
                    *link_order(src), "-o", str(out)], check=True)


def time_run(binary, source):
    start = time.perf_counter()
    subprocess.run([binary, source], capture_output=True, check=True)
    return time.perf_counter() - start


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("rev_a")
    parser.add_argument("rev_b", nargs="?", default="WORKTREE")
    parser.add_argument("--runs", type=int, default=9)
    parser.add_argument("--only", default="")
    args = parser.parse_args()
    names = [n for n in NAMES if not args.only or n in args.only.split(",")]

    with tempfile.TemporaryDirectory() as workdir:
        bin_a = Path(workdir) / "a"
        bin_b = Path(workdir) / "b"
        build(args.rev_a, bin_a, workdir)
        build(args.rev_b, bin_b, workdir)

        print(f"A = {args.rev_a}, B = {args.rev_b}, {args.runs} interleaved runs")
        print()
        print("| benchmark | A | B | speedup A/B |")
        print("|---|---|---|---|")
        ratios = []
        for name in names:
            source = str(BENCH / f"{name}.bz")
            time_run(bin_a, source)  # warm-up
            time_run(bin_b, source)
            times_a, times_b = [], []
            for _ in range(args.runs):
                times_a.append(time_run(bin_a, source))
                times_b.append(time_run(bin_b, source))
            a, b = statistics.median(times_a), statistics.median(times_b)
            ratios.append(a / b)
            print(f"| {name} | {a:.3f}s | {b:.3f}s | {a / b:.2f}× |")
            sys.stdout.flush()
        print(f"| **geomean** | | | **{statistics.geometric_mean(ratios):.2f}×** |")


if __name__ == "__main__":
    main()
