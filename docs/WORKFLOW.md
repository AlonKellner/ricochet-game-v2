# The development workflow

How a change moves from an edit to main, and what checks it at each step. The targets are in the
`Makefile`; `gates.sh` runs the gates with a timed summary. Numbers are measured on the 12-core M-series
machine (6 performance, 6 efficiency cores) unless a row says otherwise.

## The layers and what verifies them

| Layer | Source of truth | Verified by | Where it runs |
|---|---|---|---|
| Spec | `GAME_SPEC.md`, the oracle (`docs/research/oracle/trace2.py`, `visibility.py`) | pytest (`make oracle-test`, 149 tests, 2 s) | Python |
| Kernel (`native/core`) | the headers | the oracle fixtures and corpus (differential), the invariant kernels over the sweeps, the goldens (bit identity), ASan+UBSan | `rc_tests` (ctest), no Godot |
| Bindings and glue (`native/gd`, `scripts/`) | GDScript + the bindings | GUT: the extension loads, the dumps' parity, the game logic, the renderers, the LIVE sweep combos | Godot headless |
| Game | the scenes | interactive sign-off per behavioural phase | Godot |

## The three gates

| Target | When | What | Time |
|---|---|---|---|
| `make check` | after every edit | `make core` (the `rc_tests` binary alone) then the ctest entries of an area: `AREA=all` (default) / `solver` / `vis` / `goldens`, or one doctest filter `TC="..."` (`SKIP=0` adds `--no-skip`) | 18–24 s for a core header change + the entries (all: 18 s) |
| `make gate` | before every commit | `gates.sh LEVEL=commit`: core, oracle, ctest (release), the extension build, the GUT suite, the goldens unchanged | see the table below |
| `make gates` | before a merge to main, at each phase close, and whenever a golden, a fixture or a dump changes | `LEVEL=merge`: the same, then ctest under ASan+UBSan | see the table below |

Every gate runs even after a failure; the summary names the failed ones and the log directory.

## The three build trees

| Tree | Target | Contents | Why |
|---|---|---|---|
| `native/build-core` | `make core` | `ricochet_core` + `rc_tests`, RelWithDebInfo, no godot-cpp | the inner loop and every ctest gate: a core header change rebuilds about 11 test TUs |
| `native/build` | `make native` | `ricochet_core` + godot-cpp + `ricochet_gd` → `native/bin`, no tests | what Godot loads |
| `native/build-asan` | `make native-asan` | `ricochet_core` + `rc_tests`, Debug `-O1`, `-fsanitize=address,undefined`, no godot-cpp | nothing loads a sanitized extension, so the sanitizers run on the tests alone |

- A tree configured for the first time gets Ninja when installed (`brew install ninja ccache`); an existing
  tree keeps its generator (CMake cannot switch one in place: `rm -rf` the tree to move it).
- ccache is the compiler launcher when installed. It is content-keyed: reverting a header, switching a
  branch or popping a stash compiles nothing twice (a revert of `solver.hpp` rebuilds in 0.4 s).
- `JOBS` defaults to every core; a cold godot-cpp build (1,089 TUs) is bounded at 4 until its library exists
  (`JOBS_EXT`). `CTEST_JOBS` defaults to every core.
- `make native` runs `godot --headless --import` only when `.godot/extension_list.cfg` is missing or older
  than `ricochet.gdextension` (the file a headless Godot discovers the extension through).

## Measured

The inner loop, `make core` after a change (ccache cold for the changed units):

| Change | Rebuild |
|---|---|
| `solver.hpp` (fans out to 11 test TUs) | 24 s |
| `visibility.hpp` (8 TUs) | 18 s |
| a revert, a branch switch back (ccache hits) | 0.4 s |
| no change | 0.2 s |
| cold tree | 31 s |

One heavy test TU (`test_goldens.cpp`, `test_visibility.cpp`) costs 15–16 s at `-O2`: parsing 2 s, the
front end (template instantiation) about 2.5 s, the optimizer 11 s (the inliner 5 s, function passes 6 s,
spread over the three `Solver<N>` instantiations, `BigFloat`/`Dual`, nlohmann and the test's own code; no
single function dominates). The remaining lever is out-of-line definitions and explicit instantiation of
the solver in `ricochet_core` (estimated 40 % less per TU); not done.

ctest in the core tree: 26 entries, 18 s wall (the Stage 105/106 random-scene cases run in `RC_CASE_PARTS`
= 4 parts each: `RC_CASE_PART=k/n`, the k-th scene of every n, the scenes drawn as in the whole run).
ASan: a cold tree plus its ctest 97 s; the ctest alone 51 s (was 74–85 s with godot-cpp in the tree and
the two random cases whole).

## The data pipelines

| Data | Producer | Target | Consumers |
|---|---|---|---|
| `bench/scenes/*.surfaces.json` (the levels as surface defs) | `scripts/testing/dump_scene_surfaces.gd` (Godot) | by hand (`godot --headless --path . -s res://scripts/testing/dump_scene_surfaces.gd`) | the native visibility tests, the generators, `tscn_import.py`'s parity test |
| `bench/fixtures/*.json` | `docs/research/oracle/gen_fixtures_*.py` | by hand, one generator each | GUT and native fixture tests |
| `bench/scenes_corpus/{scenarios,expected}` | `docs/research/oracle/gen_scene_corpus.py` | by hand | GUT corpus test, the native corpus goldens |
| `bench/goldens/*.json` | dumped at tag `pre-native` (the script is gone); re-blessed only | `RC_SWEEP_REBLESS=1` / `RC_GOLDEN_REBLESS=1` + `docs/research/apply_rebless.py`, after a judged report the user approved | `test_goldens.cpp` |

## A stage

1. Spec: `GAME_SPEC.md`, then the oracle and its tests (`make oracle-test`), then the fixtures.
2. Native: the kernel, its fixture tests and the goldens green under `make gate`.
3. Bindings, GUT, visuals.
4. Goldens judged, approved, re-blessed.
5. Docs: the TDD doc's phase table, CHANGELOG, GAME_SPEC's version.
6. Interactive sign-off on the behavioural phases.

`make gates` closes each phase; `make gate` precedes each commit. No stage is complete without the user's
sign-off on its interactive tests (CLAUDE.md).
