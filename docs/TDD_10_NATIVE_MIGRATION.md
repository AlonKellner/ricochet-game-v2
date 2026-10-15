# TDD Document 10: Native Migration (C++ GDExtension)

**Stages 88--100** | Move the precision solver and its supporting math to a
C++ GDExtension that is bit-identical with the shipped GDScript `FastSolver`,
behind a toolchain that proves it on every run (ctest + GUT), while the game,
visual and editor layers keep speaking GDScript.

Background: `docs/TDD_09_PRECISION_INTEGRATION.md` (the solver being ported),
`docs/precision_benchmark_findings.md` (why precision needs native speed).

### Stage Status

| Stage | Topic | Status |
|-------|-------|--------|
| 88a | Infinity specified: oracle `infinity` event + ∞ plan entry, `stage88_infinity.json`, GAME_SPEC 2.11.0 | Done (spec; implementation 88b–d) |
| 89 | Toolchain (CMake, godot-cpp pinned, doctest/ctest, `NativeProbe`, runners) + goldens of the pure-GDScript stack + `SweepPlans` | Done (toolchain verified by its author; full-suite run after merge pending) |
| 90 | `BigFloat`, `F64`, `Dual`, `CN<N>`, the `Num` concept | Done: native `NumCtx` / `Num` / `CN` / `BigFloat` (+ `F64Num`, `DualNum` factories) under their final names; GDScript kept as `Gd*` until Stage 99 |
| 91 | `Exact` predicates, constructions, lazy `Line` | Done: native `Exact` flipped; `test_diff_beam_geom.gd` identical (B12 noted) |
| 92 | `Beam<N>`, segment geometry, effect maps: one template, FastSolver's f64 twin folded in | Done: `HPBeam`, `HPBeamCircle`, `BeamGeom`, `BeamGeomSeg`, `BeamEffects`, `FastSolver` flipped |
| 93 | `Flags`, `SurfaceDef`/`Scene<N>` from Variant-free defs, categorical tables, ghost | Done: `BeamScene` / `BeamSceneSurf` flipped; the Surface -> defs glue moved to `SceneDefs` (scripts/game) |
| 94 | `Solver<N>`: the walker, anchored chains, exact aim images, caches on an instance | Done: `BeamSolver` (+ `Event`, `PlanItem`, `AnchorChain` flat) flipped; identical on every fixture at 3 tiers |
| 95 | `ErrorEstimate` + `PrecisionLadder` | Done: flipped; identical on the corpus |
| 96 | `TraceStep`/`TracePath` (float32), `Tracer` (`trace_pair`, `plan_refuses`, `to_path`) | Done: `BeamTracer` is an instance per owner; `Point`/`Direction`/`Ray`/`TraceHit`/`TraceStep`/`TracePath` native; 284,096 sweep combos identical; full suite green |
| 97 | Visual/testing kernels: arc geometry, `VisualConverter`, `StepTreeMerge`, click geometry, arrow advance | Done: `GeneralizedCircle`, `Segment`, `VisualConverter`, `StepTreeMerge`, `ArrowAdvance`, `SurfaceGeom` native (float32 Vector2 semantics mirrored); `test_diff_visual.gd` identical on the sweep scenes and random inputs |
| 98 | `InvariantChecker` kernel + native sweeps (shard by combo) | Done: `rc/invariants.hpp` + `rc/sweep.hpp`; `InvariantKernel` / `SweepKernel` bindings; `InvariantChecker` is a thin driver; the plan sweeps run natively (one live combo per scene kept); `test_invariant_detects.gd`, `test_diff_invariants.gd` |
| 99 | Delete the GDScript implementations, final names, source-scan tests rewritten | Done: 25 GDScript implementation files, the Gd* test copies, the differential tests and `tools/dump_goldens.gd` deleted; `test_stage99_no_legacy.gd` scans for Gd* / N* / inner-class names and checks every registered class is a RefCounted; the goldens stay as pins |
| 88b | Regenerate fixtures with `EMIT_INFINITY`; user approves the diff | Evidence ready: `docs/research/stage88b_fixture_diff.md` (B9); **awaiting the user's approval** before the committed corpora change |
| 88c | Native `infinity` event, ∞ plan item, reversed anchored orientation | Planned |
| 88d | Edge click, hover band, plan chevrons, HUD, `level_bounds` | Planned |
| 100 | Performance pass: dirty flags, no per-frame defs, cached arc params | Done: static frames do no solver or draw work (`frame_bench.gd`: 6 µs vs 120-290 µs per frame); `test_stage100_dirty_flags.gd`; **interactive sign-off pending** |

**Regression Test Policy:** After implementing Stage N, run ALL tests (ctest and
the full GUT suite). The full suite must pass before proceeding to Stage N+1.
No exceptions.

**TDD protocol (every stage):**
1. **RED:** write the stage's specification tests first: doctest cases for the
   `rc` core with expected values from closed forms or the mpmath oracle
   (`docs/research/oracle/`, outputs in `bench/**`), and GUT tests for anything
   that crosses the binding boundary. Confirm they fail for the right reason.
2. **GREEN:** implement the minimum native code that makes them pass.
3. **REFACTOR** with ctest and the full GUT suite green.
4. **GATE:** the following must all hold:
   - ctest green in RelWithDebInfo and under ASan/UBSan (`make native-asan`);
   - full GUT suite green with the extension loaded;
   - native output bit-identical to the GDScript `FastSolver` on every fixture;
   - user sign-off on the interactive checklist.

**Bit-identity contract:** the native solver and the GDScript `FastSolver` are
twins. Every f64 operation happens in the same order with one IEEE-754 rounding
each. No fused multiply-add, no fast-math, no `long double` intermediates, no
x87. Section "FP flags policy" below is how the build enforces it.

**Feedback Loop Protocol and USER VALIDATION GATE:** as in TDD_05 (mandatory
sign-off; the user's word is final).

---

## Layout

```
native/
  CMakeLists.txt            C++20; options RC_BUILD_TESTS, RC_BUILD_EXTENSION, RC_SANITIZE
  cmake/fp.cmake            flags applied to every rc target (see FP flags policy)
  core/include/rc/*.hpp     ricochet_core public headers, namespace rc, ZERO Godot includes
  core/src/*.cpp            ricochet_core sources
  gd/*.cpp, *.hpp           ricochet_gd: godot-cpp bindings, register_types, NativeProbe
  tests/*.cpp               rc_tests (doctest), run by ctest
  tools/check_api.py        ctest rc_api_matches_binary
  tools/check_no_godot.cmake  ctest rc_core_no_godot
  godot-api/                extension_api.json + gdextension_interface.{h,json} dumped from the installed Godot (tracked)
  extern/godot-cpp          git submodule, pinned (see below)
  extern/doctest, extern/nlohmann  vendored single headers (versions in extern/README.md)
  bin/                      build output loaded by Godot (gitignored)
  build*/                   CMake build trees (gitignored)
ricochet.gdextension        repo root; Godot discovers it from here
tests/native/*.gd           GUT tests that need the extension
```

Dependency direction is strict: `ricochet_core` knows nothing about Godot
(enforced by ctest `rc_core_no_godot`); `ricochet_gd` adapts `rc` types to
Variant and back; GDScript sees only registered classes.

---

## Stage 89: Toolchain

**Goal.** A C++ GDExtension toolchain (CMake, no SCons), a Godot-free core
library, a doctest executable under ctest, a smoke-test class that exercises
every binding mechanism later stages rely on, Makefile targets and test-runner
integration. No game code is ported.

### How to build

```bash
make native        # RelWithDebInfo -> native/build, library -> native/bin/, then headless import
make ctest         # ctest in native/build (rc_tests, rc_core_no_godot, rc_api_matches_binary)
make native-debug  # Debug -> native/build-debug (RC_ASSERT aborts); same library name in native/bin
make native-asan   # Debug + -fsanitize=address,undefined -> native/build-asan, ctest there
make test-fast     # make native + ctest + every non-sweep GUT file (incl. tests/native) in one process
make test          # make native, then the whole GUT suite (sweeps included)
make test-stage STAGE=87   # exactly tests/test_stage87_*.gd
./run_tests.sh / ./run_tests_parallel.sh   # both run `make native` first and fail fast if it fails
```

Or directly:

```bash
cmake -S native -B native/build -DCMAKE_BUILD_TYPE=RelWithDebInfo
cmake --build native/build -j 12
ctest --test-dir native/build --output-on-failure
godot --headless --import --path .
godot --headless -s addons/gut/gut_cmdln.gd -gconfig= -gtest=res://tests/native/test_extension_loads.gd -gexit
```

Build parallelism is bounded (`JOBS`, default = CPU count). A bare `-j` lets
Make start every one of godot-cpp's ~1100 compiles at once; the first (unbounded)
cold build here took 25 min of mostly system time, versus a few minutes bounded.
Only the first build compiles godot-cpp; incremental builds of `rc` code take
seconds.

### godot-cpp: pinned master, 4.7 API

godot-cpp has no 4.6 or 4.7 release branch. `master` (v10.x) is the line that
ships `gdextension/extension_api-4-7.json`, so the submodule
`native/extern/godot-cpp` is `master` pinned to commit
`b8530af7546a3fcae80cef2477526569fd42ffc2` (2026-10-12). Pinning by SHA, not
branch, keeps builds reproducible; moving the pin is a deliberate commit.

**API-file decision.** `godot --headless --dump-extension-api --dump-gdextension-interface`
(run inside `native/godot-api/`) dumps the installed binary's API. Parsed as
JSON, the dump is identical to the bundled `extension_api-4-7.json`
(header `Godot Engine v4.7.stable.official`, precision `single`), and the
dumped `gdextension_interface.json` is identical to the bundled one. So
godot-cpp is built with `GODOTCPP_API_VERSION=4.7` (the bundled file), not with
`GODOTCPP_CUSTOM_API_FILE`. ctest `rc_api_matches_binary`
(`native/tools/check_api.py`, stdlib only) re-checks this on every run and
prints the verdict; if it ever fails, either re-dump the API after a Godot
upgrade or build with `-DGODOTCPP_CUSTOM_API_FILE=native/godot-api/extension_api.json`.

Other godot-cpp settings (`native/CMakeLists.txt`): `GODOTCPP_TARGET=template_debug`
(what the editor and headless runs load; `DEBUG_ENABLED`), `GODOTCPP_PRECISION=single`
(this project's `Vector2` is float32), `GODOTCPP_USE_HOT_RELOAD=ON` (the
`.gdextension` is `reloadable`), `GODOTCPP_SYSTEM_HEADERS=ON` plus
`add_subdirectory(... SYSTEM)` so godot-cpp's headers are exempt from our
`-Werror` flags.

### Headless extension discovery (verified)

A headless Godot does not scan the project for `*.gdextension`; it loads the
extensions listed in `.godot/extension_list.cfg`, and that file is written by
the editor's import step. Verified in this stage:

- fresh checkout: `.godot/extension_list.cfg` does not exist;
- `godot --headless --import --path .` exits 0 and writes it with the single
  line `res://ricochet.gdextension`;
- a subsequent `godot --headless -s addons/gut/gut_cmdln.gd ...` has
  `NativeProbe` registered already at GDScript parse time (the parser reports
  `p is Node` as a static type error for a `NativeProbe` variable).

`make native` therefore ends with the import step, and the test runners depend
on `make native`. If `.godot/` is deleted, run `make native` (or the import)
again before any headless test run.

### `ricochet.gdextension`

```
[configuration]
entry_symbol = "ricochet_library_init"
compatibility_minimum = "4.7"
reloadable = true

[libraries]
macos.debug.arm64   = "res://native/bin/libricochet_gd.macos.template_debug.arm64.dylib"
macos.release.arm64 = "res://native/bin/libricochet_gd.macos.template_release.arm64.dylib"
macos.debug.x86_64 / macos.release.x86_64, linux.debug.x86_64 / linux.release.x86_64 (.so),
windows.debug.x86_64 / windows.release.x86_64 (.dll)   -- placeholders, same naming
```

File names are exactly what CMake produces: `libricochet_gd${GODOTCPP_SUFFIX}`
where `GODOTCPP_SUFFIX` is `.<platform>.<godot-cpp target>.<arch>` (e.g.
`.macos.template_debug.arm64`). The suffix carries the architecture, so the
feature tags carry it too (`macos.debug.arm64` rather than `macos.debug`); Godot
matches `arm64`/`x86_64` as feature tags. The library is a plain `.dylib`, not a
`.framework` (that only matters for signed app bundles, Stage 100).
`native/bin/` is gitignored; every developer builds their own.

Only `template_debug` libraries exist today. The `release` entries describe
what a `-DGODOTCPP_TARGET=template_release` build produces and are filled in at
export time (Stage 100).

### FP flags policy

`native/cmake/fp.cmake` applies to every rc target (`ricochet_core`,
`ricochet_gd`, `rc_tests`):

```
-ffp-contract=off -fno-fast-math -Wall -Wextra -Wpedantic -Werror -Wshadow [-Wconversion]
```

Why: the native solver must be bit-identical with the shipped GDScript
`FastSolver`, which computes in plain IEEE-754 binary64 with one rounding per
operation. A fused multiply-add rounds `a*b+c` once instead of twice, and
fast-math licenses reassociation and reciprocal tricks; either changes results
in the last bit, which the exact predicates and the shadow run would then
flag as a mismatch. Clang contracts within a statement by default
(`-ffp-contract=on`), so the flag is not optional. `rc::fma_sensitive` plus its
doctest is the tripwire: `(1 + 2^-52)(1 - 2^-52) - 1` is `0.0` with two
roundings and `-2^-104` fused; the test also asserts `std::fma` gives the fused
value, so the triple is known to be sensitive on the host.

`-Wconversion` is on for `ricochet_core` and `rc_tests` and off for
`ricochet_gd` (godot-cpp's binding templates narrow inside macros expanded in
our translation units). Warnings are errors everywhere.

### Error-handling policy: `RC_ASSERT`

`native/core/include/rc/error.hpp`. `RC_ASSERT(cond, msg)` follows the project
rule "debug builds assert+crash on invariant violations; release builds
log+recover":

- `RC_ASSERT_ABORTS=1` (Debug builds, i.e. `make native-debug`, `make native-asan`):
  print `RC_ASSERT failed at file:line: (expr) msg` to stderr and `abort()`.
- `RC_ASSERT_ABORTS=0` (RelWithDebInfo, what Godot loads): format the same text,
  pass it to the error sink, continue.

CMake defines `RC_ASSERT_ABORTS` per configuration as a PUBLIC definition of
`ricochet_core`; without it `NDEBUG` decides. The sink is
`rc::set_error_sink(std::function<void(const char*)>)`; `rc::report_error`
routes through it, and an empty sink falls back to stderr.
`initialize_ricochet_module` sets the sink to `godot::UtilityFunctions::push_error`
and `uninitialize_ricochet_module` clears it (the library is reloadable).
`NativeProbe.sink_error_test()` fires the sink once; the GUT test asserts the
push_error arrived with `assert_push_error`. The sink path is unit-tested in
`native/tests/test_error_sink.cpp`, which compiles `RC_ASSERT` in its
non-aborting form regardless of build type.

### `NativeProbe` (what the smoke test covers)

`native/gd/gd_native_probe.{hpp,cpp}`, registered at
`MODULE_INITIALIZATION_LEVEL_SCENE`, extends `RefCounted` (not a Node):
static method `add`, static `version()` returning `rc::RC_VERSION`,
enum `Mode { PHYSICAL, PLANNED }` (`BIND_ENUM_CONSTANT`, so both
`NativeProbe.Mode.PLANNED` and `NativeProbe.PLANNED` work), integer constant
`MAX_HITS = 32` (`BIND_CONSTANT`), property `kind: String` (`ADD_PROPERTY`, so
`"kind" in obj` works), `make_child()` returning a second registered class
`NativeProbeChild` with a `value: float` property (flat registration standing
in for an inner class), `doubled(PackedFloat64Array)` (bit-exact round trip,
including `-0.0` and `1e300`), `key_count(Dictionary)`, `sink_error_test()`.
`tests/native/test_extension_loads.gd` checks each of these from GDScript.

### Commands to run everything

```bash
make native                                   # build + import
make ctest                                    # 3 ctest tests
make native-asan                              # ASan/UBSan build + ctest
godot --headless -s addons/gut/gut_cmdln.gd -gconfig= -gtest=res://tests/native/test_extension_loads.gd -gexit
make test-fast                                # ctest + all non-sweep GUT files
./run_tests_parallel.sh                       # full suite, sweeps sharded
```

### Interactive checklist (user sign-off)

- [ ] Open the project in the editor: no GDExtension load errors in Output.
- [ ] In the editor's Output, `print(NativeProbe.add(2, 3))` from a tool script prints 5.
- [ ] Rebuild (`make native`) with the editor open: the extension hot-reloads without a crash.
- [ ] `make native-asan` is green.


---

## Stage 88a: Infinity specified

The oracle (`docs/research/oracle/trace2.py`) gained `INFINITY_ID = -3` and the
`EMIT_INFINITY` switch: with it on, a straight beam passing `t = ∞` emits an
`infinity` event between the crossings ahead and the wrapped ones, counted
toward `MAX_HITS` (a cut there is a `max` with no point); the event is applied
when the next plan entry is ∞ (both modes). `via_infinity` reverses the aim
when the plan's first epoch holds an ∞ entry: every plan has two realizations
(toward the aim image, or away from it and back through ∞). Ten oracle tests,
`gen_fixtures_stage88.py` → `bench/fixtures/stage88_infinity.json` (26
fixtures, planned + physical twins), `tests/test_stage88_infinity.gd` (schema
pinned; the solver comparison pending until 88c). With the switch off every
existing fixture regenerates byte-identically. GAME_SPEC §4.2, §6.3, §11.2,
§12.5, §13.1; CHANGELOG 2.11.0.

Design note for 88d: an ∞ entry inside a ghost stretch is **allowed** (a ghost
arrow wraps); only sides are refused there (GAME_SPEC 10.6).

### Stage 88b: the fixture diff (evidence, not yet applied)

Two scratch copies of the repository at Stage 99 regenerated every corpus with
`EMIT_INFINITY` on and off; the off copy reproduces the committed files
byte-identically. The on copy differs in exactly three ways, all consequences
of the decided semantics: pure insertions of `infinity` events, traces already
at MAX_HITS cut earlier (the `max` event moves to the next old crossing), and
cuts landing on an infinity event (a `max` with no point). Counts per corpus,
every cut case by name and the GUT tests it reaches are in
`docs/research/stage88b_fixture_diff.md` (classifier: `stage88b_classify.py`).
The committed corpora stay as they are until the user approves that diff (B9);
88c then flips the oracle default, regenerates, and makes the native solver
emit the event.

## Stage 89 additions: goldens, SweepPlans, machine quirks

- `tools/dump_goldens.gd` dumps the pure-GDScript stack's outputs as bit
  patterns: `bench/goldens/{bigfloat,exact,beam,fixtures,corpus,chain,sweeps}.json`
  (`--check` regenerates and compares; `--part=`; `--sweep-stride=`). Every
  float input is recorded as hex (`inputs`) because **Godot 4.7 does not round
  decimal-to-double conversion correctly**: `921.3028564453125` (a float32-exact
  value) parses one ulp low as a GDScript literal, via `String.to_float` and via
  `JSON.parse_string`. The game never parses (inputs are float32 `Vector2`), so
  gameplay is unaffected, but native tests must start from the recorded bits.
- `scripts/testing/sweep_plans.gd`: one definition of the swept scenes, the
  mirror / inversion / projective plan generators and the 30 combo
  configurations, shared by the sweep tests, the violation viewer and the dumper.
- `override.cfg` + `tools/ca_bundle_override.pem`: Godot loads system CA
  certificates from the macOS keychain at startup (`SecTrustCopyAnchorCertificates`);
  on this machine that call stalls for minutes when `securityd` is blocked, which
  hung every headless run. The bundle override skips the keychain (startup
  >30 s → 1 s). Whether `--import` (editor mode) is covered is still to be
  checked.
- GDScript quirk (found by the toolchain work): within one function the
  constant pool merges `0.0` and `-0.0`; build `+0.0` at runtime in tests.
- Never run several Godot processes concurrently, and bound `cmake --build -j`
  (an unbounded build of godot-cpp thrashed the machine for 25 minutes).

## Stages 90–96: the core port (verified so far)

One `Num` concept with three tiers: `F64`, `Dual` (the shipped float64 walk,
GDScript `FastSolver`), `BigFloat`. Kernels are written once; the few rules
where FastSolver's hand-written derivatives differ from mechanical dual
arithmetic are explicit `Dual` branches (`Backend<N>`, `Beam::at_infinity`,
`effects::project` for circle normals), and the shipped float64 walk's
conventions are reproduced: parameters from `param_of` carry no derivative, an
exact hit point takes the beam's own derivative, a decision margin uses the
display-safe point, and an event's beam identity is a fresh view whenever the
raw beam object changes (`Event::beam_view`).

| Golden | Checks | Result |
|---|---|---|
| BigFloat ops, 6 tiers | 4578 | identical |
| Exact predicates / constructions / lazy lines | 6800 | identical |
| Beam / segment / effect kernels (F64 vs generic and FastSolver, Dual derivatives, 4 limbs) | 24261 | identical |
| Every stage fixture (f64, float64 walk with gains, 4 limbs) | 345 traces | identical |
| Corpus (549): float64 walks, ladder tiers/estimates/convergence, 4-limb walks | 7137 | identical |
| Anchored / image / pole chains of the main corpus | 9154 | identical |

`native/tests/test_goldens.cpp` runs all of it under ctest in 2.6 s (49739
assertions); the same corpus pass takes 139 s in GDScript.

### The bindings (`native/gd/`) and the differential GUT tests

Flat `N`-prefixed `RefCounted` classes for the dual-implementation period
(section 1.6): GDScript inner classes register flat (`BeamSolver.Event` →
`NBeamSolverEvent`, `FastSolver.Scene` → `NFastSolverScene`,
`PrecisionLadder.Result` → `NPrecisionLadderResult`, `BeamTracer.Pair` →
`NBeamTracerPair`). Conventions that the flip's mechanical edit must apply:

- GDExtension has no static properties: float constants and the tier list are
  static methods (`ErrorEstimate.C` → `C()`, `BeamTracer.TOL` → `TOL()`,
  `PrecisionLadder.TIERS` → `tiers()`). Integer constants and enums bind as
  usual (`NBeamSolver.MAX_HITS`, `NBeamSolver.PLANNED`).
- One scalar class per tier family: `NNum` carries an `F64`, `Dual` or
  `BigFloat` value (`v`, `d`, `sign`/`exp`/`limbs`), `NCN` a complex of one
  tier, `NNumCtx` (limbs 0 / `DUAL_LIMBS` = -1 / n) makes them. `NHPBeam`,
  `NBeamScene` and `NBeamSolverEvent` are tier variants too; mixing tiers in
  one call is reported through the error sink.
- `NBeamTracer` is an instance that owns its caches (no statics; one per
  `PathRenderer` / `GameManager` at the flip) and takes scene **defs**
  (`BeamScene.defs_from_surfaces`), not game `Surface` objects. `NTraceHit`
  carries `surface_id` instead of a `Segment`; `NRay.origin` and
  `NDirection.start/end` are `Vector2`, not `Point`.
- Plan arguments accept `NBeamSolverPlanItem`, GDScript `BeamSolver.PlanItem`
  and `PlanManager.PlanEntry` alike (`surface_id`, `left` or `side`, `hint` or
  `click`), so tests and glue need no conversion during the dual period.

| Test (`tests/native/`) | Compares | Scope | Result |
|---|---|---|---|
| `test_diff_beam_solver.gd` | `NBeamScene` surfaces; `NBeamSolver` events (every field, points / parameters / margins bit for bit, flags, beam identity partition); `NFastSolver` events + gains; `plan_effects`, `plan_flags`, `fixes_cursor`, `targets_of` | 115 fixtures × {F64, 4 limbs, float64 walk} | identical |
| `test_diff_precision_ladder.gd` | `NPrecisionLadder.solve` (tiers, events, errors, climb record, sensitivity), `solve_physical`, `verify` mode; `NErrorEstimate` sensitivities, `estimate`, `unreliable_index`, `required_limbs`, `replay`; `event_distance`, `planned_residuals` | corpus (every 5th scenario; `DIFF_ALL=1` for all) | identical |
| `test_diff_beam_tracer.gd` | `NBeamTracer.trace_pair` pairs: both `TracePath`s step for step (float32 bits, frames, types, hits, rays, meta), ladder result, final flags; `plan_refuses` | the sweep scenes / positions / plans of `SweepPlans`, every 97th combo (`DIFF_STRIDE=1` = the full sweep) | identical |

`tests/native/diff_helpers.gd` holds the comparators (`events_diff`,
`pair_diff`, ...); each returns the first difference as one line, so a failing
run names the fixture, the event and the field.

### Stage 96 gate: the sweep goldens (284,096 combos)

`bench/goldens/sweeps.json` now records each group's inputs (defs as parsed,
positions as float32 hex, the plan); the doctest "goldens: the sweep combos of
Tracer.trace_pair" replays every combo of the ten sweep tests through
`rc::Tracer`, canonicalizes the pair exactly as `tools/dump_goldens.gd` does and
compares SHA-256 prefixes (vendored `tests/support/sha256.hpp`), plus the two
full sample strings per group. 284,096 combos in about 20 s; `RC_SWEEP_STRIDE`
thins it.

The first run found 2 mismatches, both in the climb record (`tiers_tried`
[4, 8, 16, 64] vs [4, 8, 16, 32, 64]) of max-tier traces: Godot's `maxf(a, b)`
is `(a > b) ? a : b`, so a NaN gain (a tangent root's 0/0 derivative in the
replay) replaces the envelope, whereas `std::max` keeps the envelope. The
estimate then reads NaN, `required_limbs` jumps to the last tier. The core now
uses `rc::gd_maxf` / `gd_minf` wherever GDScript wrote `maxf` / `minf`
(envelopes, estimates, residual floors, margin minima, `_worst`), and the
sweep goldens are identical. Whether a NaN gain should rather count as
"unknown, climb one tier" is B-list material (B2 neighbourhood); the shipped
behaviour is reproduced.

### Stages 91-92 bindings: `NExact`, `NHPBeamCircle`, `NBeamGeomSeg`, `NBeamGeom`, `NBeamEffects`

`tests/native/test_diff_beam_geom.gd` compares segments, roots, hit records,
topology decisions, beam constructions (`through`, `invert_point`,
`invert_at`), the raw float64 kernels of `FastSolver` (`NFastSolver` now
carries every raw static), every effect map on the solver fixtures' surfaces
(`apply`, `portal`, `project`, `projective_exit`, back-propagation, `map_point`,
`map_pole`, `portal_point`) and the exact kernels, at F64 and 4 limbs. Two
findings:

- **Signed zeros in derivatives.** FastSolver's hand-written derivatives keep
  a `-0.0` where dual arithmetic adds a `+0.0` term (`dtx * 1.0` versus
  `dtx + tx * 0.0`), e.g. a seeded beam with `ty = 0`. Values are bit-identical;
  derivatives only reach the gains as norms, so the sign is unobservable. The
  raw-kernel comparisons treat `±0` derivative components as equal.
- **B12: `Exact.U` is not 2^-53.** `exact.gd` writes `1.1102230246251565e-16`,
  which Godot parses to `0x3c9ffffffffffffe` (two ulps below 2^-53; Python and
  C++ parse the same literal to exactly 2^-53). Every running error bound and
  filter threshold (`emul`, `Line.from_exact`, the concurrency filter) is
  relatively 2e-16 too small. The native core reproduces the shipped bit
  pattern; the fix (`pow(2, -53)` on both sides, or the hex literal) is a
  behaviour change for approval. The only long decimal literal in the ported
  GDScript.

### The flip (Stages 90-96 together)

The core classes depend on each other through their scalar and beam types, so
they flip together: the native classes register under the final names and the
GDScript originals become `Gd*` (`scripts/math/precision/*.gd`, `point.gd`,
`direction.gd`, `ray.gd`, `trace_hit.gd`, `trace_step.gd`, `trace_path.gd`),
kept for the differential tests and `tools/dump_goldens.gd` until Stage 99.
What changed outside the core (220 mechanical edits in 40 test files and the
glue, plus a few by hand):

- **Inner classes are flat**: `BeamSolver.Event` -> `BeamSolverEvent`,
  `BeamSolver.PlanItem` -> `BeamSolverPlanItem`, `BeamSolver.Mode.X` ->
  `BeamSolver.X`, `BeamSolver.AnchorChain.build(...)` ->
  `BeamSolver.anchor_chain(...)`, `FastSolver.Scene` -> `FastSolverScene`,
  `BeamScene.Surf` -> `BeamSceneSurf`, `HPBeam.Circle` -> `HPBeamCircle`,
  `BeamGeom.Seg` -> `BeamGeomSeg`, `BeamGeom.Kind.X` -> `BeamGeom.X`,
  `PrecisionLadder.Result` -> `PrecisionLadderResult`,
  `ErrorEstimate.Sensitivity` -> `ErrorEstimateSensitivity`,
  `BeamTracer.Pair` -> `BeamTracerPair`.
- **Non-integer constants are methods**: `Exact.U()`, `ErrorEstimate.C()`,
  `SCALE()`, `MARGIN_SAFETY()`, `BeamTracer.TOL()`, `PrecisionLadder.tiers()`,
  `BeamScene.GHOST()`, `BeamScene.TRANSFORMATIVE()` / `PROJECTIVE()` (also on
  `BeamSolver`). Integer constants and enums are unchanged (`MAX_HITS`,
  `PLANNED`, `BigFloat.BITS`, `FastSolver.K`).
- **One scalar class**: `Num` carries an F64, Dual or BigFloat value;
  `F64Num.of`, `DualNum.of`, `BigFloat.from_float` / `from_string` / `from_int`
  / `zero` / `infinity` are factories returning `Num`. Type hints `: BigFloat`
  / `-> F64Num` became `Num`.
- **Private helpers tests used are public**: `Exact.emul/eadd/esub`,
  `BeamGeom.eq/grad`.
- **`SceneDefs`** (`scripts/game/scene_defs.gd`) now holds
  `defs_from_surfaces`, `describe_surface`, `describe_config`,
  `describe_resolver` and `segments_by_id`: the only place the solver's inputs
  are read from game `Surface`s. The math layer no longer depends on the game
  layer (A-list).
- **`BeamTracer` is an instance** owning its caches (`GameManager` and
  `PathRenderer` each hold one; `DebugDumper` makes one per dump); it takes
  defs and the id -> `Segment` map (`trace_pair(defs, player, cursor, entries,
  flags, segments)`). The static caches are gone (A-list). Tests go through
  `TestTrace.trace_pair` / `TestTrace.plan_refuses` (fresh tracer, game
  surfaces); the 22 `BeamTracer.clear_cache()` calls in tests were deleted.
- **Trace data classes are native** with the GDScript shapes (`Point.coords`,
  `Direction.start/end`, `Ray.origin/direction`, `TraceHit.point/segment/side`,
  `TraceStep` meta fields, `TracePath.targets_hit` as a Dictionary) and are
  built with `TraceStep.make(...)`, `TraceHit.make(...)`, `Ray.make` /
  `from_coords`, `Direction.make` / `from_coords`, `Point.at` (GDExtension
  classes take no constructor arguments). Consumers (`VisualConverter`,
  `StepTreeMerge`, `ArrowAnimator`, `InvariantChecker`, `DebugDumper`) run
  unchanged apart from `.new(` -> `.make(`.
- **Identity semantics preserved**: events of one walk on the same beam share
  one `HPBeam` object (a popped frame restores the earlier object; the
  invariant checker compares `a.beam != b.beam`), every step of a pair shares
  one aim `Ray` object, and a one-trajectory pair shares its step objects
  between `planned` and `physical` (`BeamTracer.trace_pair` duplicated the
  Array). `BeamSolverEvent.point` / `.beam` are settable (the
  deliberate-violation tests rotate a beam and offset a point).
- The differential tests now compare `Gd*` against the final names; they, the
  goldens dumper and the `Gd*` files go away in Stage 99.
- Import-time noise: the editor documents the extension on `--import` and
  warns "Instantiated CN used as default value for ..." for object-typed
  properties of default instances. Harmless; every getter answers on a default
  instance (an earlier null dereference in `BeamSceneSurf` aborted the import).

**Gate after the flip:** `./run_tests_parallel.sh`: 756 passing, 0 failing,
65 s wall (the GDScript suite took about 5 minutes; the sweeps now trace
natively through `PathRenderer`). `make test-fast`: 696 passing in 90 s
(ctest 23 s of it, the 284k-combo sweep check included). Shadow mode is the
full-stride differential run: `DIFF_STRIDE=1` on
`tests/native/test_diff_beam_tracer.gd` compares `GdBeamTracer` with
`BeamTracer` on every sweep combo through the bindings and the live scenes.

## Stage 97: visual and testing kernels

`rc/visual.hpp` ports the display-side geometry: `GeneralizedCircle` /
`Segment` (carrier derivation, `determine_side`, winding, `full_from_carrier`,
the click distance that was `ClickDetector._distance_to_segment`),
`VisualConverter` (`compute_display_end`, `project_onto_carrier`,
`prepare_for_display` as a plan of pieces the binding materializes with the
source steps' hit and ray objects, `clip_to_bounds`, `is_arc`, `arc_params`),
`ArrowAnimator.advance` (with `_arc_reentry`) and `SurfaceNode`'s sampling
statics. The rule of the port: a `Vector2` operation is float32 exactly as the
engine computes it (`V2`: `angle` = `atan2f`, `lerp`, `rotated` with `sinf` /
`cosf`, `normalized` with the `l != 0` guard; `Rect2f.grow` / `has_point`), a
GDScript scalar is a double, and a component read enters scalar arithmetic as
a double. Noted quirks reproduced: `prepare_for_display` leaves `hit_count` at
0 and shares the source path's `targets_hit` Dictionary; `_piece` does not copy
`ghost`; `_snap_step` returns the very same step object when nothing moves.

Bindings and names: `GeneralizedCircle.make(a, b, c, d)` (was `.new`),
`Segment.make(start, end, via)` / `from_coords` / `full_from_carrier` (the
carrier object is kept, so `get_carrier() == carrier`), `Segment.distance_to`,
`Segment.winding()`; `VisualConverter.DEFAULT_BOUNDS()` and `MAX_ARC_RADIUS()`
are methods, `POINTS_PER_FULL_CIRCLE` a constant; `ArrowAdvance.advance(...)`
returns an `ArrowAdvanceResult` and `ArrowAdvance.start_direction(step)` is
the arrow's initial heading (`ArrowAnimator` keeps only the node glue);
`SurfaceGeom.line_sample / arc_sample / chevron_vertices` (the `SurfaceNode`
statics delegate); `TracePath.physical_hits()` (`PathRenderer.build_physical_hits`
delegates). The GDScript originals are `GdSegment`, `GdGeneralizedCircle`,
`GdVisualConverter`, `GdStepTreeMerge`, plus test-support copies under
`scripts/testing/gd_kernels/` (`GdArrowAdvance`, `GdClickGeom`,
`GdSurfaceGeom`, `GdPhysicalHits`), all deleted in Stage 99.

`tests/native/test_diff_visual.gd` compares both stacks bit for bit: carriers,
sides, winding and click distances on 400 random segments (lines, full lines,
arcs, full circles); arc params, projections and clipping on 500 random
triples and three bounds; surface samples on 300 random inputs; and on the
sweep scenes' traces (every 37th combo, `DIFF_STRIDE`) the display paths at
two bounds, display ends, arc params, start directions, arrow flights in
random increments, physical hits, merged and classified steps.

## Stage 98: the invariant kernel and the native sweeps

`rc/invariants.hpp` is `InvariantChecker.check_all` as a function of data: the
display paths, the merged typed steps, the ladder's events (tier-agnostic
`EvInfo` records: float64 values, beam identity by `beam_view`), the surfaces
(points, carrier, "a trace may not cross it") and the F64 scene. A violation is
the GDScript format string with its arguments; `InvariantKernel` renders it
with Godot's `%` operator, so the messages are the GDScript checker's to the
character (`%s` of a Vector2, a float, a bool or a nested Array included).
The B4 carrier-distance formula of TRACE-ENDS is reproduced as it is.

`rc/sweep.hpp` runs a whole sweep natively: trace pair, display paths
(`prepare_for_display` on core paths), merged steps and `check_all` per
combo, sharded by player index exactly as `SweepRunner.in_shard`. Positions
stay in GDScript (`SweepRunner.build_positions`: Godot's RNG). GDScript side:
`InvariantChecker` is a thin driver (positions the nodes, sets the plan, lets
`PathRenderer` trace, hands the kernel what the renderer exposes, plus
`surface_infos` with the per-flags "has effect"); `SweepRunner.sweep` and
`sweep_native` call `SweepKernel`; the plan / projective / combo sweeps run
natively and keep one combo per scene through the live renderer. The
inversion-grid precision sweep stays live (its extra LONG-ARC-CHORD check reads
the renderer's display paths). `check_S11` / `check_S12` (unused) are gone
(A-list). The GDScript checker survives as `GdInvariantChecker`
(`scripts/testing/gd_kernels/`) for the differential test.

Tests: `tests/native/invariant_cases.gd` holds hand-corrupted traces, one or
more per invariant, fed to a fake renderer; `tests/test_invariant_detects.gd`
asserts the kernel trips each (and nothing on the clean one) and that the
event checks catch an offset point, a rotated beam and an unapplied plan;
`tests/native/test_diff_invariants.gd` asserts the GDScript checker, the
driver and `SweepKernel` return identical strings on those cases, on 40
random event walks with corruptions, and on every 41st combo of the sweep
scenes (`DIFF_STRIDE`). Findings while matching: Godot 4.7's
`Vector2.normalize` zeroes non-finite and zero-length vectors (now mirrored by
`V2::normalized`; the goldens are unchanged), a hit without a `Segment` is a
hit on no surface for VISUAL-ON-CARRIER, and two checkers on one scene without
a GameManager each install their own plan (a harness pitfall, not a kernel
difference). VIA_ON_ARC cannot be tripped by a consistent (start, via, end)
triple: `arc_params` orients the arc so the via lies inside the span.

**Gate:** `./run_tests_parallel.sh` 766 passing / 0 failing in 39 s (65 s before
the native sweeps; about 5 minutes in GDScript); `make test-fast` 706 passing;
ctest 50131 assertions.

## Stage 99: the GDScript implementations are gone

Deleted: `scripts/math/precision/` (15 files), the trace data classes
(`point.gd`, `direction.gd`, `ray.gd`, `trace_hit.gd`, `trace_step.gd`,
`trace_path.gd`), `segment.gd`, `generalized_circle.gd`, `step_tree_merge.gd`,
`scripts/visual/visual_converter.gd`, `scripts/testing/gd_kernels/`, the six
`tests/native/test_diff_*.gd` with `diff_helpers.gd`, and `tools/dump_goldens.gd`.
`bench/goldens/*.json` stay as the regression pins `native/tests/test_goldens.cpp`
replays (50131 assertions); they were produced by the GDScript stack at tag
`pre-native` and are regenerated only by an approved B-item, by checking out
that tag's dumper. `tests/test_stage99_no_legacy.gd` replaces the differential
tests' role as the gate against drift back: deleted files stay deleted, no
`Gd*` / `N*` / inner-class names in scripts, tests or tools, every registered
extension class is a RefCounted and not a Node (the native-era
`test_stage1_math_layer.gd`), and the goldens exist. Kept until their own
B-items: `TestTrace` / `TraceMode` (B6) and `TraceHit.at_endpoint` /
`blocked_*`.

**Gate (all green on 2026-10-15, commit `8d96433`):**

- `./run_tests_parallel.sh`: 734 passing / 0 failing in 11 s wall (the GDScript
  suite took about 5 minutes of wall time and 24 minutes of CPU).
- `ctest` (RelWithDebInfo): 23 cases, 50131 assertions, about 23 s; the sweep
  goldens replay all 284,096 combos.
- `make native-asan JOBS=4` + `ctest --test-dir native/build-asan`: 3/3 passed,
  548 s, no AddressSanitizer / UndefinedBehaviorSanitizer / LeakSanitizer report.
- `make bench` (`--tag=main`, 161 v2 scenarios): identical to the table recorded
  in TDD_09 at tag `pre-native`: ladder and ladder_verify 161/161 planned and
  physical; beam_f64 planned 161/161, physical 112/161; beam_big112 planned
  161/161, physical 160/161.
- oracle: `pytest -q docs/research/oracle` 76 passed.

`native-migration` merged into `main` (`--no-ff`) at this point, per the plan;
the second merge follows Stage 100.

## Stage 100: the performance pass (dirty keys)

Before this stage every frame rebuilt the scene defs (`SceneDefs.defs_from_surfaces`
and `segments_by_id`, one Dictionary per surface), ran the two solver traces,
the display preparation and the merge, redrew the path, ran the click detector
for the hover (with `plan_refuses` and its own defs per candidate), rebuilt the
plan overlays and redrew every surface node, whether or not anything had
changed. Now each of those runs only when its inputs changed:

- `SceneDefsCache` (scripts/game): the defs and the id -> Segment map of a
  surfaces array, built once per array (identity + size: scenes append their
  surfaces in `_ready` and never edit a Surface afterwards); `version` counts
  the rebuilds. Used by `PathRenderer`, `GameManager` and `InvariantChecker`.
- `PathRenderer._process` compares `[player, cursor, plan entries by value,
  flags (a deep copy: the state may be edited in place), defs version]` with
  the last computation and recomputes only on a change; `trace_version` counts
  the recomputations; `queue_redraw()` only then (and on a display-mode
  change). A direct `_compute_trace()` call always recomputes, as the invariant
  checker and the tests rely on. The equality is Godot's: Dictionary and Array
  `==` compare contents (checked by `test_stage100_dirty_flags.gd`).
- `GameManager._update_hover` runs on `[cursor, plan.version, flags]` changes,
  `_update_surface_overlays` on `[trace_version, plan.version, flags]` changes
  (forced by `_refresh_ui`, i.e. after a click, undo or reset).
  `PlanManager.version` is bumped by every mutation.
- `SurfaceNode` keeps its overlays when the new ones are equal
  (`ChevronOverlay.equals` / `same_list`), so no redraw; its arc parameters
  are computed once per setup. `Cursor` no longer calls `queue_redraw()` every
  frame (a moved CanvasItem does not need one).
- The edge walls of `LevelSettings` and the display bounds of `PathRenderer`
  come from `level_bounds` instead of literal 1920x1080 / `DEFAULT_BOUNDS`
  (B5). Zero diff today: no scene overrides `level_bounds` (grep of every
  `.tscn`) and the default equals `DEFAULT_BOUNDS`; the test pins both.

`scripts/testing/frame_bench.gd` (`make frame-bench [SCENE=] [FRAMES=]`) loads
a scene headlessly and runs six phases of 250 frames: static, cursor orbit, a
two-entry plan static and orbiting, hover resting on a surface, player orbit.
It reports the wall time per frame (hook to hook of the SceneTree, with the
headless frame pacing turned off: `OS.low_processor_usage_mode_sleep_usec =
0`; `Performance.TIME_PROCESS` is a once-per-second maximum and useless here)
and the recomputation count, fails if a static phase recomputes after its
setup frame or a mean frame exceeds 8 ms. On this machine (`main.tscn`, 13
surfaces): static 0.006 ms, cursor orbit 0.121 ms, plan orbit 0.258 ms,
player orbit 0.109 ms per frame; `projective_arcs.tscn`: 0.006 / 0.157 /
0.288 / 0.201 ms. The orbit columns are what every frame cost before the
stage.

**Gate:** `./run_tests_parallel.sh` 750 passing / 0 failing in 11 s wall;
ctest unchanged (no native change). The dirty paths are compared with fresh
computations in `test_stage100_dirty_flags.gd` (16 tests: positions, plan,
flags replaced and edited in place, player on cursor, direct recompute, overlay
and hover stability, the caches, the plan version, the edge walls).

**Interactive checklist (user sign-off):**
1. Idle scene: the preview, hover and plan chevrons look as before; the
   process view of the editor (or Activity Monitor) shows the game near idle
   while nothing moves.
2. Move the cursor: the preview follows every frame; hover highlights appear
   and disappear exactly at the 24 px band; the partner side of a portal
   highlights too.
3. Plan: click sides, right-click to remove, C to clear; the chevrons and
   plan numbers update at once; the HUD matches.
4. Fire, let the arrow land (and speed it up with a key): the state flags
   promote, the surfaces recolour, the preview updates after landing; Z (undo)
   and R (reset) restore the preview and overlays.
5. Tab cycles the display modes immediately (no cursor move needed).
6. Flag scenes (`flag_switches.tscn`, `ghost_room.tscn`): toggling a flag by a
   shot recolours the gates and mirrors and changes the preview.

## Machine quirks: the open editor and the migration

The Godot editor (launched from VSCode) stayed open through Stages 90-97. It
does not rescan while unfocused; when it did, it found 44 changed scripts (two
class flips) and a rebuilt `libricochet_gd` whose registered classes had all
changed names (`NBeamSolver` -> `BeamSolver`, ...). With `reloadable = true`
it hot-reloaded that library; a class-set swap is not something GDExtension
hot reload supports ("cannot change parent type ... Restart Godot", "Some
extensions need the editor to restart"), and the editor reset itself with
errors. `ricochet.gdextension` now has `reloadable = false`: the editor asks
for a restart when the library changes. Also, `godot --headless --import`
(run by `make native`) rewrites `.godot/` while the editor has it open; run
the suite with the editor closed, or restart the editor afterwards. The five
`Godot-*.ips` crash reports of that night are headless test processes (the
two aborts fixed in Stages 93 and 96), not the editor.

## B-list: behaviour changes awaiting approval (found during the port)

| # | Change | Where | Evidence |
|---|---|---|---|
| B1 | `BigFloat.mul` int64 column overflow above ~128 limbs per factor (reproduced natively) | `big_float.gd:251-256` | doctest with `__int128` reference |
| B12 | `Exact.U` parsed two ulps below 2^-53 by Godot | `exact.gd:11` | `test_diff_beam_geom.gd` (`Exact.U` bits), `tools/dump_goldens.gd` hex inputs; native `exact.hpp` comment |
| B9 | The `infinity` event counts toward MAX_HITS: 443 events inserted across the corpora, 79 traces at MAX_HITS cut earlier (182 old tail events dropped), stress corpus unchanged; no visible-output change until 88c | oracle `trace2.py` `EMIT_INFINITY` | `docs/research/stage88b_fixture_diff.md` (per-case list), `stage88b_classify.py` |
| B5 | `PathRenderer` display bounds and `LevelSettings` edge walls from `level_bounds` instead of 1920x1080 literals (needed by 88d) | `path_renderer.gd`, `level_settings.gd` | **applied in Stage 100 with zero diff**: no `.tscn` sets `level_bounds`, the default equals `DEFAULT_BOUNDS`; `test_stage100_dirty_flags.gd` pins both; the user may veto |
| B13 | A NaN replay gain poisons the envelope through `maxf` and jumps the ladder to the last tier | `error_estimate.gd:111`, `precision_ladder.gd` | sweep goldens `precision/no_plan` combos 4883, 4919 (`tiers_tried`) |
