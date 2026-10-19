GODOT ?= $(if $(GODOT_BIN),$(GODOT_BIN),godot)
CMAKE ?= cmake
CTEST ?= ctest
NCPU := $(shell sysctl -n hw.ncpu 2>/dev/null || nproc 2>/dev/null || echo 4)
# compiles: every core, except a cold godot-cpp build (1089 TUs; a bare -j once
# took 25 min swapping): 4 until its library exists
JOBS ?= $(NCPU)
GODOTCPP_LIB := $(wildcard native/build/bin/libgodot-cpp*.a)
JOBS_EXT ?= $(if $(GODOTCPP_LIB),$(JOBS),4)
# ctest entries in parallel: every core (the entries are single-threaded)
CTEST_JOBS ?= $(NCPU)
# a tree configured for the first time gets Ninja when installed (an existing
# tree keeps its generator: CMake refuses to switch one in place; rm -rf it to move)
NINJA := $(shell command -v ninja 2>/dev/null)
gen = $(if $(wildcard $(1)/CMakeCache.txt),,$(if $(NINJA),-G Ninja,))

comma := ,
empty :=
space := $(empty) $(empty)

BUILD_CORE := native/build-core
BUILD_EXT := native/build
BUILD_ASAN := native/build-asan

.PHONY: test run test-verbose test-stage test-fast bench bench-stress frame-bench oracle-test \
	core check native native-debug native-asan ctest gates gate manifests judge rebless regen regen-apply dumps \
	sessions sessions-check

# ---------------------------------------------------------------- native (C++)
# The three trees (docs/WORKFLOW.md):
#   build-core  rc_tests alone, no godot-cpp: the inner loop and every ctest gate
#   build       the GDExtension Godot loads (ricochet.gdextension -> native/bin/), no tests
#   build-asan  rc_tests under ASan+UBSan, no godot-cpp

# rc_tests (RelWithDebInfo). A core header change rebuilds about 11 TUs of 2 s.
core:
	$(CMAKE) -S native -B $(BUILD_CORE) $(call gen,$(BUILD_CORE)) -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DRC_BUILD_EXTENSION=OFF -DRC_BUILD_TESTS=ON
	$(CMAKE) --build $(BUILD_CORE) -j $(JOBS)

# The inner loop: core, then the ctest entries of an area, or one doctest filter.
#   make check                      # every entry (~20 s)
#   make check AREA=solver|vis|goldens|sweeps|game
#   make check TC="Stage 111*"      # rc_tests -tc=<filter> (--no-skip with SKIP=0)
AREA ?= all
TC ?=
check_re_all := .
check_re_solver := ^rc_tests$$|tier_independence|pop_tiers|sweep_goldens
check_re_vis := visibility
check_re_goldens := ^rc_tests$$|sweep_goldens
check_re_sweeps := rc_sweeps
check_re_game := ^rcg
check: core
	@if [ -n "$(TC)" ]; then $(BUILD_CORE)/rc_tests -tc="$(TC)" $(if $(filter 0,$(SKIP)),--no-skip,); \
	else $(CTEST) --test-dir $(BUILD_CORE) -j $(CTEST_JOBS) --output-on-failure -R '$(check_re_$(AREA))'; fi

# The extension (RelWithDebInfo) loaded by Godot. The headless --import makes a
# headless Godot discover it (.godot/extension_list.cfg); it runs when that file
# is missing or older than ricochet.gdextension (2 s otherwise saved).
native:
	$(CMAKE) -S native -B $(BUILD_EXT) $(call gen,$(BUILD_EXT)) -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DRC_BUILD_EXTENSION=ON -DRC_BUILD_TESTS=OFF
	$(CMAKE) --build $(BUILD_EXT) -j $(JOBS_EXT)
	@if [ ! -s .godot/extension_list.cfg ] || [ ricochet.gdextension -nt .godot/extension_list.cfg ]; then \
		$(GODOT) --headless --import --path .; else echo "import: extension_list.cfg current"; fi

# Debug extension (RC_ASSERT aborts). Writes the same library name into native/bin,
# so it replaces the RelWithDebInfo one until the next `make native`.
native-debug:
	$(CMAKE) -S native -B native/build-debug $(call gen,native/build-debug) -DCMAKE_BUILD_TYPE=Debug \
		-DRC_BUILD_EXTENSION=ON -DRC_BUILD_TESTS=OFF
	$(CMAKE) --build native/build-debug -j $(JOBS_EXT)
	$(GODOT) --headless --import --path .

# ASan+UBSan rc_tests (no godot-cpp: nothing loads a sanitized extension), the
# ctest entries in parallel. Debug (assertions on) at -O1: the sanitizers catch
# the same errors many times faster than at -O0. ASAN_SWEEP_STRIDE=n replays one
# golden sweep combo in n for a quicker look; ASAN_SWEEPS_STRIDE=n runs the
# native sweeps (bench/sweeps) at one player in n (default 24: the sanitizers
# want the code paths, not every combo; the release ctest runs them all). The
# regions' digest parts (bit-identity pins, 53 s sanitized) are left out: the
# level loops exercise the same regions.
ASAN_SWEEP_STRIDE ?= 1
ASAN_SWEEPS_STRIDE ?= 24
native-asan:
	$(CMAKE) -S native -B $(BUILD_ASAN) $(call gen,$(BUILD_ASAN)) -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_CXX_FLAGS_DEBUG="-g -O1" -DCMAKE_C_FLAGS_DEBUG="-g -O1" \
		-DRC_SANITIZE=address,undefined -DRC_BUILD_EXTENSION=OFF -DRC_BUILD_TESTS=ON
	$(CMAKE) --build $(BUILD_ASAN) -j $(JOBS)
	RC_SWEEP_STRIDE=$(ASAN_SWEEP_STRIDE) RC_SWEEPS_STRIDE=$(ASAN_SWEEPS_STRIDE) $(CTEST) --test-dir $(BUILD_ASAN) -j $(CTEST_JOBS) \
		--output-on-failure -E rc_tests_regions

# the release ctest (build-core; `make core` first)
ctest:
	$(CTEST) --test-dir $(BUILD_CORE) -j $(CTEST_JOBS) --output-on-failure

# Every gate before a merge or a phase close (gates.sh LEVEL=merge), with a timed
# summary; `make gate` is the commit gate (LEVEL=commit).
gates:
	LEVEL=merge ./gates.sh
gate:
	LEVEL=commit ./gates.sh

# ------------------------------------------------------------- sweep manifests
# Workflow W2: the invariant sweeps' inputs as bench/sweeps/*.json, replayed
# natively by rc_tests (sweeps:*): expanded from data/sweeps.json (the sweeps'
# call sites) and data/levels by the headless host (Stage 116, no Godot).
# Regenerate after a change to the levels or the recipes; the manifests gate
# (gates.sh) fails while they are stale.
MANIFEST_DIR ?= $(CURDIR)/bench/sweeps
manifests: core
	rm -rf "$(MANIFEST_DIR)" && mkdir -p "$(MANIFEST_DIR)"
	$(BUILD_CORE)/rc_tool manifests data/sweeps.json data/levels "$(MANIFEST_DIR)"

# ------------------------------------------------------------- session replay
# Stage 115 (docs/research/session_digest.md): the game played through the
# session scripts (data/sessions, docs/research/sessions/gen_sessions.py), one
# record of its observable state per step -> bench/sessions/*.jsonl, the
# golden every engine-agnostic port reproduces. `sessions` re-records it (a
# changed record is a behaviour change: the judge's report first),
# `sessions-check` records into a temporary directory and compares
# (docs/research/session_judge.py, exit 2 on a difference). The shards run in
# parallel Godot processes.
SESSIONS_DIR ?= $(CURDIR)/bench/sessions
SESSION_JOBS ?= $(NCPU)
sessions: native
	rm -rf "$(SESSIONS_DIR)"
	tools/record_sessions.sh "$(SESSIONS_DIR)" $(SESSION_JOBS)
	@ls "$(SESSIONS_DIR)" | wc -l | sed 's/^ *//; s/$$/ sessions/'
sessions-check: native
	@d=$$(mktemp -d "$${TMPDIR:-/tmp}/ricochet_sessions.XXXXXX"); \
	tools/record_sessions.sh "$$d" $(SESSION_JOBS) && python3 -I docs/research/session_judge.py bench/sessions "$$d" && rm -rf "$$d"

# ------------------------------------------------------------- goldens judge
# Workflow W4: what the tree changes against REF (default main), dumped from
# a cached reference build and classified (tools/judge.sh, docs/research/judge.py)
# -> native/build-ref/judge/report.md. `make rebless APPROVED=1` applies the
# judged re-bless lines (docs/research/apply_rebless.py) after the user approved
# the report; `make gates` then passes the goldens gate once they are committed.
REF ?= main
RULES ?=
judge: core
	REF=$(REF) RULES=$(RULES) tools/judge.sh
rebless:
	@if [ "$(APPROVED)" != 1 ]; then echo "!! make rebless APPROVED=1: only after the user approved native/build-ref/judge/report.md"; exit 1; fi
	@[ -s native/build-ref/judge/rebless.txt ] || { echo "!! no re-bless lines: make judge first"; exit 1; }
	$(ORACLE_PY) docs/research/apply_rebless.py native/build-ref/judge/rebless.txt

# ------------------------------------------------------------------ oracle
# The mpmath oracle's own tests and the fixture schema (docs/research/oracle).
ORACLE_PY := docs/research/.venv/bin/python
# Workflow W3: every fixture generator and the scene corpus, compared
# semantically with the committed files (docs/research/regen.py): `regen`
# reports and restores, `regen-apply` keeps the files whose content changed
# (a byte-only difference is never committed). ONLY=stage111 narrows.
ONLY ?=
regen:
	$(ORACLE_PY) -I docs/research/regen.py $(if $(ONLY),--only $(ONLY),)
regen-apply:
	$(ORACLE_PY) -I docs/research/regen.py --keep $(if $(ONLY),--only $(ONLY),)
# the levels as surface defs (bench/scenes/*.surfaces.json): the generators'
# and the native visibility tests' input, built from the level data by the
# headless host (Stage 115, no Godot); the dumps gate fails while they are stale
DUMPS_DIR ?= $(CURDIR)/bench/scenes
dumps: core
	$(BUILD_CORE)/rc_tool dumps data/levels "$(DUMPS_DIR)"
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
SWEEPS := test_invariant_sweep.gd test_invariant_combos.gd test_precision_invariant_sweep.gd test_stage107_level_sweep.gd test_stage108_level_sweep.gd test_visibility_sweep.gd
FAST_TESTS := $(filter-out $(addprefix tests/,$(SWEEPS)),$(wildcard tests/test_*.gd)) \
	$(wildcard tests/native/test_*.gd)
test-fast: core native ctest oracle-test
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
