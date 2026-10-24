GODOT ?= $(if $(GODOT_BIN),$(GODOT_BIN),godot)
# Stage 119: the Godot project, a host of the native game (docs/PORTING.md)
GODOT_PROJECT := engines/godot
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

.PHONY: violations quick gate-clear-cache test run test-verbose frame-bench perf perf-diff oracle-test pyhost \
	core check native native-debug native-asan ctest gates gate manifests judge rebless regen regen-apply dumps \
	sessions sessions-check godot-data

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
#   make check AREA=solver|vis|goldens|sweeps|game (game: rcg_* and the C API's capi_*)
#   make check TC="Stage 111*"      # rc_tests -tc=<filter> (--no-skip with SKIP=0)
AREA ?= all
TC ?=
check_re_all := .
check_re_solver := ^rc_tests$$|tier_independence|pop_tiers|sweep_goldens
check_re_vis := visibility
check_re_goldens := ^rc_tests$$|sweep_goldens
check_re_sweeps := rc_sweeps
check_re_game := ^rcg|^capi
check: core
	@if [ -n "$(TC)" ]; then $(BUILD_CORE)/rc_tests -tc="$(TC)" $(if $(filter 0,$(SKIP)),--no-skip,); \
	else $(CTEST) --test-dir $(BUILD_CORE) -j $(CTEST_JOBS) --output-on-failure -R '$(check_re_$(AREA))'; fi

# Between commits: core, then every ctest entry a change can affect (the commit
# gate's ctest: the result cache, tools/gate_cache.py; the sweeps at one player
# in 4, the asynchronous and C API session replays every 4th session)
quick: core
	RC_SWEEPS_STRIDE=4 RC_SESSIONS_SAMPLE=4 python3 -I tools/gate_cache.py ctest $(BUILD_CORE) --jobs $(CTEST_JOBS) $(if $(AREA),--regex '$(check_re_$(AREA))',)
gate-clear-cache:
	python3 -I tools/gate_cache.py clear

# The extension (RelWithDebInfo) loaded by Godot. The headless --import makes a
# headless Godot discover it (.godot/extension_list.cfg); it runs when that file
# is missing or older than ricochet.gdextension (2 s otherwise saved).
native:
	$(CMAKE) -S native -B $(BUILD_EXT) $(call gen,$(BUILD_EXT)) -DCMAKE_BUILD_TYPE=RelWithDebInfo \
		-DRC_BUILD_EXTENSION=ON -DRC_BUILD_TESTS=OFF -DRC_EXTENSION_OUTPUT_DIR=$(CURDIR)/$(GODOT_PROJECT)/bin
	$(CMAKE) --build $(BUILD_EXT) -j $(JOBS_EXT)
	@$(MAKE) -s godot-data
	@if [ ! -s $(GODOT_PROJECT)/.godot/extension_list.cfg ] || [ $(GODOT_PROJECT)/ricochet.gdextension -nt $(GODOT_PROJECT)/.godot/extension_list.cfg ]; then \
		$(GODOT) --headless --import --path $(GODOT_PROJECT); else echo "import: extension_list.cfg current"; fi

# Stage 119: the Godot project (engines/godot) reads only res://: the levels,
# the session scripts and their golden are copied in (gitignored).
godot-data:
	mkdir -p $(GODOT_PROJECT)/data
	rsync -a --delete data/levels data/sessions $(GODOT_PROJECT)/data/
	rsync -a --delete bench/sessions/ $(GODOT_PROJECT)/data/golden/

# Debug extension (RC_ASSERT aborts). Writes the same library name into native/bin,
# so it replaces the RelWithDebInfo one until the next `make native`.
native-debug:
	$(CMAKE) -S native -B native/build-debug $(call gen,native/build-debug) -DCMAKE_BUILD_TYPE=Debug \
		-DRC_BUILD_EXTENSION=ON -DRC_BUILD_TESTS=OFF -DRC_EXTENSION_OUTPUT_DIR=$(CURDIR)/$(GODOT_PROJECT)/bin
	$(CMAKE) --build native/build-debug -j $(JOBS_EXT)
	@$(MAKE) -s godot-data
	$(GODOT) --headless --import --path $(GODOT_PROJECT)

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
# the asynchronous and C API session replays: every n-th session (the release
# ctest of `make gates` plays them all; the synchronous replay plays all here)
ASAN_SESSIONS_SAMPLE ?= 4
native-asan:
	$(CMAKE) -S native -B $(BUILD_ASAN) $(call gen,$(BUILD_ASAN)) -DCMAKE_BUILD_TYPE=Debug \
		-DCMAKE_CXX_FLAGS_DEBUG="-g -O1" -DCMAKE_C_FLAGS_DEBUG="-g -O1" \
		-DRC_SANITIZE=address,undefined -DRC_BUILD_EXTENSION=OFF -DRC_BUILD_TESTS=ON
	$(CMAKE) --build $(BUILD_ASAN) -j $(JOBS)
	RC_SWEEP_STRIDE=$(ASAN_SWEEP_STRIDE) RC_SWEEPS_STRIDE=$(ASAN_SWEEPS_STRIDE) RC_SESSIONS_SAMPLE=$(ASAN_SESSIONS_SAMPLE) $(CTEST) --test-dir $(BUILD_ASAN) -j $(CTEST_JOBS) \
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
# golden every host reproduces. Since Stage 119 the native game records it
# (rc_tool replay, through the C API). `sessions` re-records it (a changed
# record is a behaviour change: docs/research/session_judge.py's report first),
# `sessions-check` compares a replay with it (also ctest capi_sessions_*).
SESSIONS_DIR ?= $(CURDIR)/bench/sessions
# Stage 129: the sweeps' violations (the known findings' too, marked) for the
# violation viewer (engines/godot/scenes/violation_viewer.tscn): FILTER=<text>
# narrows the manifests (RC_SWEEPS_FILTER), e.g. make violations FILTER=crossings__4
VIOLATIONS := engines/godot/violations.json
violations: core
	-RC_SWEEPS_FILTER="$(FILTER)" RC_SWEEPS_VIOLATIONS=$(BUILD_CORE)/violations_invariant.json $(BUILD_CORE)/rc_tests -tc="sweeps: the invariant sweeps' manifests hold on every combo" > /dev/null
	-RC_SWEEPS_FILTER="$(FILTER)" RC_SWEEPS_VIOLATIONS=$(BUILD_CORE)/violations_visibility.json $(BUILD_CORE)/rc_tests -tc="sweeps: the visibility sweeps' manifests hold on every combo" > /dev/null
	python3 -I -c 'import json, sys; v = [e for f in sys.argv[2:] for e in json.load(open(f))]; json.dump(v, open(sys.argv[1], "w"), indent=1); print(len(v), "violations ->", sys.argv[1], "(open scenes/violation_viewer.tscn)")' $(VIOLATIONS) $(BUILD_CORE)/violations_invariant.json $(BUILD_CORE)/violations_visibility.json

sessions: core
	rm -rf "$(SESSIONS_DIR)"
	$(BUILD_CORE)/rc_tool replay data/sessions data/levels "$(SESSIONS_DIR)"
sessions-check: core
	$(BUILD_CORE)/rc_tool replay data/sessions data/levels --check bench/sessions

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

# Stage 118: the Python host (tests/host_py, ctypes over the C API's shared
# library): every session through the C API equals the golden, the errors, the draw list
pyhost: core
	@if [ -x $(ORACLE_PY) ]; then $(ORACLE_PY) -m pytest -q tests/host_py; \
	else echo "!! pyhost tests skipped: no docs/research/.venv (see oracle-test)"; fi
oracle-test:
	@if [ -x $(ORACLE_PY) ]; then $(ORACLE_PY) -m pytest -q docs/research/oracle; \
	else echo "!! oracle tests skipped: no docs/research/.venv (python3 -m venv docs/research/.venv && docs/research/.venv/bin/pip install mpmath numpy pytest)"; fi

# ------------------------------------------------------------------ the Godot suite
# Stage 119: the Godot host alone (tests/test_adapter_smoke.gd, tests/test_session_replay.gd)
test: native oracle-test
	SKIP_BUILD=1 SKIP_ORACLE=1 ./run_tests_parallel.sh

run:
	$(GODOT) --path $(GODOT_PROJECT)

test-verbose:
	$(GODOT) --headless --path $(GODOT_PROJECT) -s addons/gut/gut_cmdln.gd -glog=3

# ------------------------------------------------------------------ benchmarks
# The game's per-frame cost on a level (Stage 119, rc_tool bench): still and
# moving phases; a still frame must not recompute, a mean frame stays under budget.
LEVEL ?= data/levels/main.json
FRAMES ?= 250
frame-bench: core
	$(BUILD_CORE)/rc_tool bench $(LEVEL) --frames $(FRAMES)

# Stage 120: every level's phases -> build/perf/perf.{json,md} (not committed:
# timing is evidence, never a gate); compare two reports with perf-diff.
PERF_DIR ?= build/perf
PERF_FRAMES ?= 120
perf: core
	$(BUILD_CORE)/rc_tool bench --all data/levels --frames $(PERF_FRAMES) --json $(PERF_DIR)/perf.json --md $(PERF_DIR)/perf.md
perf-diff:
	@if [ -z "$(A)" ] || [ -z "$(B)" ]; then echo "usage: make perf-diff A=old.json B=new.json"; exit 2; fi
	python3 -I tools/perf_diff.py $(A) $(B)

# The cast-cost program: every plannable side's region on every level (rc_tool
# regions) -> build/perf/regions.{json,md}: time, casts by kind, refinements,
# phases, the batches' tail, union+fill, climbs, a hash per plan. perf-diff
# compares two reports (the hashes equal: the same regions bit for bit).
#   make regions [THREADS=8] [PLAYERS=6] [REGIONS_ARGS="--pairs --allocs"]
#   make profile LEVEL=data/levels/test_levels/crossings.json [THREADS=1] [SECONDS_=8] [ONLY=plan]
THREADS ?=
PLAYERS ?= 6
regions: core
	$(BUILD_CORE)/rc_tool regions --all data/levels --players $(PLAYERS) $(if $(THREADS),--threads $(THREADS),) $(REGIONS_ARGS) \
		--json $(PERF_DIR)/regions$(if $(THREADS),-t$(THREADS),).json --md $(PERF_DIR)/regions$(if $(THREADS),-t$(THREADS),).md
profile: core
	THREADS=$(if $(THREADS),$(THREADS),1) LEVEL=$(LEVEL) SECONDS_=$(SECONDS_) PLAYERS=$(PLAYERS) ONLY=$(ONLY) PATS=$(PATS) OUT=$(PERF_DIR) ./tools/profile.sh
