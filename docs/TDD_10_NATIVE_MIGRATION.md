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
| 88a | Migration plan, layer boundaries, bit-identity contract | Planned |
| 89 | Toolchain: CMake, godot-cpp, doctest/ctest, `NativeProbe`, runners | In progress |
| 90 | Core numeric types (`Vec2`, `Complex`, generalized circle) in `rc` | Planned |
| 91 | Exact predicates and `BigFloat` port | Planned |
| 92 | Effect maps (`beam_effects`) port, FastSolver twin tests | Planned |
| 93 | `BeamSolver` rule walker port, oracle fixtures via nlohmann/json | Planned |
| 94a | `PrecisionLadder` and `ErrorEstimate` port | Planned |
| 94b | `BeamTracer`: events to `TracePath`/`TraceStep` | Planned |
| 95 | GDExtension bindings for the solver API (flat classes) | Planned |
| 96 | Shadow run: native vs GDScript on every trace, `shadow_mismatches.json` | Planned |
| 97 | Switch-over: game layer calls the native solver | Planned |
| 98 | Sweep and benchmark parity under the native solver | Planned |
| 99 | Delete the GDScript solver duplicates, keep the spec tests | Planned |
| 88b | Plan revision after shadow-run findings | Planned |
| 88c | Plan revision after switch-over | Planned |
| 88d | Plan revision after deletion | Planned |
| 100 | Export templates: linux/windows builds, CI, release checklist | Planned |

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
