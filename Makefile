# Breeze build.
#
#   make            optimized interpreter  -> build/release/breeze
#   make debug      ASan + UBSan build      -> build/debug/breeze
#   make run        run FILE (default: test.txt) with the release build
#   make test       regression suite once (tests/run.sh)
#   make check      every gate: warnings, sanitizers, GC stress, -O2, switch
#   make bench      cross-language benchmarks (BENCH_ARGS="--langs breeze")
#   make ab REV=x   interleaved A/B of revision x against the working tree
#   make clean      remove build/release and build/debug
#   make help       show this list
#
# Extra compiler flags: make CFLAGS=-DDEBUG_PRINT_CODE

ifeq ($(origin CC),default)
CC := gcc
endif

CSTD := -std=c2x
WARN := -Wall -Wextra -pedantic

# Fixed branch-target alignment keeps the dispatch loop's speed stable across
# unrelated edits; same flags as bench/run.py and bench/ab.py.
RELEASE_FLAGS := -O2 -DNDEBUG -falign-jumps=32 -falign-labels=32 -falign-loops=32
DEBUG_FLAGS := -O0 -g -fsanitize=address,undefined -fno-sanitize-recover=undefined

# virtual_machine.c is linked first so the dispatch loop's address does not
# move when another file changes size.
SRC := src/virtual_machine.c $(filter-out src/virtual_machine.c,$(sort $(wildcard src/*.c)))
RELEASE_OBJ := $(SRC:src/%.c=build/release/obj/%.o)
DEBUG_OBJ := $(SRC:src/%.c=build/debug/obj/%.o)

FILE ?= test.txt
REV ?= HEAD
BENCH_ARGS ?=

.PHONY: all release debug run test check bench ab clean help

all: release

release: build/release/breeze

debug: build/debug/breeze

build/release/breeze: $(RELEASE_OBJ)
	$(CC) $(RELEASE_FLAGS) $(LDFLAGS) $^ -o $@

build/debug/breeze: $(DEBUG_OBJ)
	$(CC) $(DEBUG_FLAGS) $(LDFLAGS) $^ -o $@

build/release/obj/%.o: src/%.c | build/release/obj
	$(CC) $(CSTD) $(WARN) $(RELEASE_FLAGS) $(CFLAGS) -Isrc -MMD -MP -c $< -o $@

build/debug/obj/%.o: src/%.c | build/debug/obj
	$(CC) $(CSTD) $(WARN) $(DEBUG_FLAGS) $(CFLAGS) -Isrc -MMD -MP -c $< -o $@

build/release/obj build/debug/obj:
	mkdir -p $@

run: build/release/breeze
	./build/release/breeze $(FILE)

test:
	tests/run.sh

check:
	tests/check-all.sh

bench:
	python3 bench/run.py $(BENCH_ARGS)

ab:
	python3 bench/ab.py $(REV)

clean:
	rm -rf build/release build/debug

help:
	@sed -n 's/^#   //p' $(firstword $(MAKEFILE_LIST))

-include $(RELEASE_OBJ:.o=.d) $(DEBUG_OBJ:.o=.d)
