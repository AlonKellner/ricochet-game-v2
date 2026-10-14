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
| 90 | `BigFloat`, `F64`, `Dual`, `CN<N>`, the `Num` concept | Core done, bit-identical (4578 golden checks); bindings + GUT diff pending |
| 91 | `Exact` predicates, constructions, lazy `Line` | Core done, bit-identical (6800); bindings + GUT diff pending |
| 92 | `Beam<N>`, segment geometry, effect maps: one template, FastSolver's f64 twin folded in | Core done, bit-identical (24261); bindings + GUT diff pending |
| 93 | `Flags`, `SurfaceDef`/`Scene<N>` from Variant-free defs, categorical tables, ghost | Core done; bindings + GUT diff pending |
| 94 | `Solver<N>`: the walker, anchored chains, exact aim images, caches on an instance | Core done, bit-identical on every fixture (345) and corpus chain (9154); bindings + GUT diff pending |
| 95 | `ErrorEstimate` + `PrecisionLadder` | Core done, bit-identical on the corpus (7137 incl. tiers/estimates); bindings pending |
| 96 | `TraceStep`/`TracePath` (float32), `Tracer` (`trace_pair`, `plan_refuses`, `to_path`) | Core written; verification against the sweep goldens pending (needs the goldens' per-group inputs: regeneration interrupted) |
| 97 | Visual/testing kernels: arc geometry, `VisualConverter`, `StepTreeMerge`, click geometry, arrow advance | Planned |
| 98 | `InvariantChecker` kernel + native sweeps (shard by combo) | Planned |
| 99 | Delete the GDScript implementations, final names, source-scan tests rewritten | Planned |
| 88b | Regenerate fixtures with `EMIT_INFINITY`; user approves the diff | Planned |
| 88c | Native `infinity` event, ∞ plan item, reversed anchored orientation | Planned |
| 88d | Edge click, hover band, plan chevrons, HUD, `level_bounds` | Planned |
| 100 | Performance pass: dirty flags, no per-frame defs, cached arc params | Planned |

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

Pending for these stages: the sweep-goldens comparison of `Tracer` (the
goldens must be regenerated with per-group inputs: 19 minutes, one Godot
process), the GDExtension bindings (`N*` classes, flat inner classes), the
`tests/native/test_diff_*.gd` differential GUT tests, shadow mode, and the
per-class flips.
