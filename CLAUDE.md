# Ricochet Game v2

## Stack

- **Game**: engine-agnostic native C++20 (`native/core`, `native/game`) behind a C API (`native/capi/include/rc_api.h`); porting guide: `docs/PORTING.md`
- **Engine host**: Godot 4.7+ (`engines/godot`; the host is one GDExtension node, `native/hosts/godot`)
- **Testing**: doctest/ctest (`rc_tests`, `rcg_tests`); pytest for the mpmath oracle and the Python host on the C API (`tests/host_py`); GUT 9.6.0 for the Godot host only
- **IDE**: VSCode

## Architecture

Layers with a strict dependency direction: core -> game -> C API -> hosts. Everything but `native/hosts/godot` and
`engines/godot` is engine-free (ctest `*_no_engine`); the Godot host includes only `rc_api.h` (ctest
`godot_adapter_includes`) and the Godot project holds at most 300 lines of GDScript outside tests (ctest `gdscript_budget`).

- `native/core/include/rc/` — The native core, header-only C++. The trajectory solver is the only solver: `solver.hpp` is the rule walker (spec: `docs/research/oracle/trace2.py`), `ladder.hpp` the dynamic precision, `tracer.hpp` turns events into `TracePath`/`TraceStep`, `exact.hpp` and `bigfloat.hpp` the exact predicates and arithmetic. Decisions use provenance and exact predicates, never epsilons. `invariants.hpp` and `sweep.hpp` are the invariant kernel; `visibility.hpp` and `visibility_checks.hpp` the visibility region and its invariants; `visual.hpp` the display geometry.
- `native/game/include/rcg/` — The engine-free game: the level data (`level.hpp`: `rc-level/1`, `docs/LEVEL_FORMAT.md`), its builder and scene defs (`build.hpp`), the surface model (`model.hpp`), crossings and pieces (`geom.hpp`), the landing (`landing.hpp`), the world (`world.hpp`), the player's movement (`kinematics.hpp`), the game session (`session.hpp`: `rcg::Session`, live play and the record), the draw list (`render.hpp`), the session digest (`digest.hpp`), the sweep manifests (`sweeps.hpp`), the canonical JSON spelling (`canon.hpp`), the solver's inputs/outputs as JSON (`trace_json.hpp`).
- `native/capi/` — `rc_api.h`: the game behind a plain C ABI (size-prefixed structs, borrowed views, statuses; `docs/PORTING.md`).
- `native/hosts/headless/` — `rc_tool`: `fmt [--check]`, `dumps`, `manifests`, `replay` (the sessions through the C API; `--check` the golden), `bench` (the per-frame cost).
- `native/hosts/godot/` — The Godot host: `RicochetGame` (reads the level, maps input to events, advances the game, follows the camera, paints the draw list).
- `engines/godot/` — The Godot project: `project.godot`, `scenes/` (one `RicochetGame` per level, `level_path`), `tests/` (the host's GUT suite), `scripts/testing/` (engine-reference recorders). `make native` builds the extension into `engines/godot/bin` and copies the data into `engines/godot/data`.
- `data/levels/` — The levels (JSON, the source of truth). `data/sessions/` — the session scripts of the replay golden (`bench/sessions`, `docs/research/session_digest.md`).
- `tests/host_py/` — The Python host (ctypes over the C API): every session, the errors, the draw list.

## Testing

The protocol: `docs/WORKFLOW.md` (the three gates, the three build trees, the data pipelines).

```bash
# The inner loop: the test binaries alone (no godot-cpp), then ctest entries of an area
make check                      # every entry, ~18 s after the build
make check AREA=vis             # solver | vis | goldens | sweeps | game | all
make check TC="visibility digest" SKIP=0   # one doctest filter (--no-skip with SKIP=0)

# Between commits: only the ctest entries a change can affect (the result cache, tools/gate_cache.py)
make quick

# Before every commit (core, oracle, ctest, the extension, the dumps/manifests current, the Python host,
# the Godot host's suite, goldens unchanged; through the result cache, redundant hosts sampled; timed summary)
make gate
SKIP_GODOT=1 make gate          # every engine-free gate (what a port runs)

# Before a merge to main or a phase close: everything, uncached and unsampled, plus ASan+UBSan
make gates

# The pieces
make core && make ctest         # rc_tests, rcg_tests, rc_tool, the C API (native/build-core) and their ctest
make native                     # the Godot extension (native/build -> engines/godot/bin), data copy, import
make native-asan                # the tests under ASan+UBSan (native/build-asan)
make oracle-test                # the mpmath oracle's pytest
make pyhost                     # the Python host on the C API
./run_tests_parallel.sh         # the Godot host's GUT suite
make sessions-check             # the session replay golden (make sessions re-records: a behaviour change)
make frame-bench                # the per-frame cost on a level (rc_tool bench)
make regions [THREADS=1]        # every plannable side's region on every level: time, casts, phases, a hash per plan (rc_tool regions)
make profile LEVEL=data/levels/test_levels/crossings.json [THREADS=1]   # a level's regions under sample: self and inclusive tables
make violations [FILTER=crossings__4]   # the sweeps' violations -> engines/godot/violations.json (open scenes/violation_viewer.tscn)
```

- Native tests: doctest `TEST_CASE`s in `native/tests/*.cpp` (`rc_tests`) and `native/tests/game/*.cpp` (`rcg_tests`).
- The retired GUT suite: every former test function's fate is in `docs/research/gut_ledger.tsv` (ctest `gut_ledger`).
- Godot host tests: `engines/godot/tests/test_*.gd`, extending `GutTest`.
- **Regression policy**: After Stage N, ALL tests from Stages 1 through N must pass before proceeding.

## Stage Workflow

Each stage follows the feedback loop protocol (the gates and targets: `docs/WORKFLOW.md`):

1. Spec first: `GAME_SPEC.md`, then the oracle (`docs/research/oracle`) and its tests, then the fixtures (`make regen`)
2. Native: the kernel and its tests green under `make check`, then `make gate` before each commit (small green commits on a branch)
3. The game layer, the C API, the hosts
4. Goldens: `make judge`, the user's approval of the report, `make rebless APPROVED=1` (the session golden: `session_judge.py`, then `make sessions`)
5. Docs: the stage's TDD doc (phase table), CHANGELOG, GAME_SPEC's version; `make gates` closes each phase
6. User performs interactive tests and gives pass/fail feedback
7. Convert failing interactive tests into automated tests; fix and re-run until all pass
8. Stage complete

**No stage is complete without user sign-off on interactive tests.** A changed golden, region digest, session
record or fixture is a behaviour change: bring the evidence (the judge's report) before applying it.

## Code Style

- C++20, the style of the surrounding code (tabs, short comments naming the GDScript or spec original where there was one)
- Numbers: positions are float32 (`rc::V2`), with GDScript's semantics kept where a float64 scalar meets a vector
- Error handling: debug builds assert+crash on invariant violations; release builds log+recover (`rc/error.hpp`; the C API returns statuses)
- Complex numbers represented as `(real, imag)` pairs
- Coordinate system: Y-down, X-right, 1 unit = 1 pixel

## Key References

- `GAME_SPEC.md` — Single source of truth for game design (25 core principles in section 2)
- `docs/PORTING.md` — The engine contract: hosting the game in another engine
- `docs/TDD_01_FOUNDATION.md` through `docs/TDD_08_EDITOR_AND_POLISH.md` — Stage-by-stage implementation guide (TDD_06 is history)
- `docs/TDD_09_PRECISION_INTEGRATION.md` through `docs/TDD_19_FORK_RULE.md` — The beam solver, the native core, hit effects, crossings, spec import, visibility
- `docs/TDD_20_ENGINE_AGNOSTIC_DATA.md` through `docs/TDD_24_ENGINE_SWITCH.md` — The engine-agnostic port
- `docs/TDD_25_FRAME_COST.md` onward — Smoother frames: the measurement harness, asynchrony (`rcg/jobs.hpp`), presentation
- `docs/TDD_42_CAST_COST.md` onward — The cast-cost program: regions under the frame budget (`rc_tool regions`, `make profile`)
