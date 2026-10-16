GODOT ?= $(if $(GODOT_BIN),$(GODOT_BIN),godot)
CMAKE ?= cmake
CTEST ?= ctest
# bounded parallelism: a bare `-j` lets Make start every godot-cpp compile at once
JOBS ?= $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)

.PHONY: test run test-verbose test-stage test-fast bench bench-stress frame-bench oracle-test \
	native native-debug native-asan ctest

# ---------------------------------------------------------------- native (C++)
# RelWithDebInfo build loaded by Godot (ricochet.gdextension -> native/bin/).
# The headless --import afterwards is what makes a headless Godot discover the
# extension: it writes .godot/extension_list.cfg, which later headless runs read.
native:
	$(CMAKE) -S native -B native/build -DCMAKE_BUILD_TYPE=RelWithDebInfo
	$(CMAKE) --build native/build -j $(JOBS)
	$(GODOT) --headless --import --path .

# Debug build (RC_ASSERT aborts). Writes the same library name into native/bin,
# so it replaces the RelWithDebInfo one until the next `make native`.
native-debug:
	$(CMAKE) -S native -B native/build-debug -DCMAKE_BUILD_TYPE=Debug
	$(CMAKE) --build native/build-debug -j $(JOBS)
	$(GODOT) --headless --import --path .

# ASan+UBSan build of everything (godot-cpp included), ctest run in place. Its
# library goes to native/build-asan/bin so it never replaces the loadable one.
# Debug (assertions on) at -O1: the sanitizers catch the same errors many times
# faster than at -O0 (the whole ctest, every sweep combo included, in ~1 min
# instead of ~10-40). The ctest entries run in parallel; ASAN_SWEEP_STRIDE=n
# replays one sweep combo in n for a quicker look. Run it alone: other jobs on
# the machine multiply its wall time.
ASAN_SWEEP_STRIDE ?= 1
native-asan:
	$(CMAKE) -S native -B native/build-asan -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_CXX_FLAGS_DEBUG="-g -O1" -DCMAKE_C_FLAGS_DEBUG="-g -O1" \
		-DRC_SANITIZE=address,undefined \
		-DRC_EXTENSION_OUTPUT_DIR=$(CURDIR)/native/build-asan/bin
	$(CMAKE) --build native/build-asan -j $(JOBS)
	RC_SWEEP_STRIDE=$(ASAN_SWEEP_STRIDE) $(CTEST) --test-dir native/build-asan -j $(JOBS) --output-on-failure

ctest:
	$(CTEST) --test-dir native/build -j $(JOBS) --output-on-failure

# ------------------------------------------------------------------ oracle
# The mpmath oracle's own tests and the fixture schema (docs/research/oracle).
ORACLE_PY := docs/research/.venv/bin/python
oracle-test:
	@if [ -x $(ORACLE_PY) ]; then $(ORACLE_PY) -m pytest -q docs/research/oracle; \
	else echo "!! oracle tests skipped: no docs/research/.venv (python3 -m venv docs/research/.venv && docs/research/.venv/bin/pip install mpmath numpy pytest)"; fi

# ------------------------------------------------------------------ GUT tests
test: native oracle-test
	$(GODOT) --headless -s addons/gut/gut_cmdln.gd

run:
	$(GODOT)

test-verbose:
	$(GODOT) --headless -s addons/gut/gut_cmdln.gd -glog=3

# Exact stage files (tests/test_stage<N>_*.gd), not a substring match.
test-stage:
	@if [ -z "$(STAGE)" ]; then echo "Usage: make test-stage STAGE=1"; exit 1; fi
	@files="$$(ls tests/test_stage$(STAGE)_*.gd 2>/dev/null | sed 's#^#res://#' | paste -sd, -)"; \
	if [ -z "$$files" ]; then echo "No tests match tests/test_stage$(STAGE)_*.gd"; exit 1; fi; \
	echo "$$files" | tr ',' '\n'; \
	$(GODOT) --headless -s addons/gut/gut_cmdln.gd -gconfig= -gtest="$$files" -gexit

# ctest plus every non-sweep GUT file in one process (the sweeps are the slow
# tests; run_tests_parallel.sh shards them).
SWEEPS := test_invariant_sweep.gd test_invariant_combos.gd test_precision_invariant_sweep.gd test_stage107_level_sweep.gd
FAST_TESTS := $(filter-out $(addprefix tests/,$(SWEEPS)),$(wildcard tests/test_*.gd)) \
	$(wildcard tests/native/test_*.gd)
comma := ,
empty :=
space := $(empty) $(empty)
test-fast: native ctest oracle-test
	$(GODOT) --headless -s addons/gut/gut_cmdln.gd -gconfig= \
		-gtest=$(subst $(space),$(comma),$(addprefix res://,$(FAST_TESTS))) -gexit

# ------------------------------------------------------------------ benchmarks
# Precision benchmark: GDScript strategies vs the mpmath oracle corpus
bench:
	$(GODOT) --headless --path . -s res://scripts/testing/run_precision_bench.gd -- --strategies=beam_f64,beam_big112,ladder,ladder_verify --tag=main

# Stage 100: the per-frame cost of a live scene, headless; static phases must
# not recompute the trace (dirty flags) and the mean frame stays under budget.
SCENE ?= res://scenes/main.tscn
FRAMES ?= 250
frame-bench: native
	$(GODOT) --headless --path . -s res://scripts/testing/frame_bench.gd -- --scene=$(SCENE) --frames=$(FRAMES)

bench-stress:
	$(GODOT) --headless --path . -s res://scripts/testing/run_precision_bench.gd -- --dir=stress --strategies=beam_f64,ladder,ladder_verify --tag=stress
