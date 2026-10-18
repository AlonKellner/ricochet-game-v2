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
| `make check` | after every edit | `make core` (the `rc_tests` binary alone) then the ctest entries of an area: `AREA=all` (default) / `solver` / `vis` / `goldens` / `sweeps`, or one doctest filter `TC="..."` (`SKIP=0` adds `--no-skip`) | 18–24 s for a core header change + the entries (all: 18 s) |
| `make gate` | before every commit | `gates.sh LEVEL=commit`: core, oracle, ctest (release; the native sweeps at one player in 4), the extension build, the manifests current, the GUT suite (its sweeps sampled), the goldens unchanged | see below |
| `make gates` | before a merge to main, at each phase close, and whenever a golden, a fixture or a dump changes | `LEVEL=merge`: the same with every sweep player, then ctest under ASan+UBSan | see below |

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

## The sweeps

The invariant and visibility sweeps (`tests/test_invariant_sweep.gd`, `test_invariant_combos.gd`,
`test_stage107_level_sweep.gd`, `test_stage108_level_sweep.gd`, `test_visibility_sweep.gd`) build their
inputs in Godot (the scene's defs and surface infos, the positions from the grid, Godot's RNG and the points
of interest, the plans from `SweepPlans`) and hand them to the native kernel (`SweepKernel.run` →
`rc::sweep::run`). Those inputs are data:

- **`bench/sweeps/<scene>__<n>.json`** (136 manifests, 1.5 MB): one per `SweepRunner.sweep_native` call,
  written under `SWEEP_MANIFEST_DIR` (`make manifests`, a dry GUT pass of the five files, 14 s). A manifest
  holds the defs, the infos (segment and carrier as the binding reads them), the positions, the plans, the
  flags and the switches (`skip_equal`, `emit_infinity`, `visibility`, `carrier_cursors`), in Godot's
  full-precision JSON.
- **`native/tests/test_sweeps.cpp`** replays every manifest through `rc::sweep::run` as the binding calls it:
  the `rc_sweeps_inv_{k}` (6 parts) and `rc_sweeps_vis_{k}` (12 parts) ctest entries, `RC_SWEEPS_PART=k/n`
  over the players, `RC_SWEEPS_STRIDE=s` thinning them (the commit gate: 4), `RC_SWEEPS_FILTER=text`,
  `RC_SWEEPS_VIOLATIONS=path` for the violation viewer's JSON. `make check AREA=sweeps`: 57 s wall, about
  580 s CPU, 386k invariant and 105k visibility combos (the main branch, 2026-10-18).
- **The manifests gate** (`gates.sh`, both levels) regenerates the manifests into a temp dir and diffs them
  against `bench/sweeps`: the native sweep checks exactly the combos the GUT sweep would. After a change to
  a level, `SweepPlans`, `SweepRunner`, `SceneDefs` or `InvariantChecker.surface_infos`: `make manifests`
  and commit the result.
- **The GUT sweep files stay as the bindings' sample**: `SWEEP_SAMPLE=8` (set by `run_tests_parallel.sh`)
  thins every shard to one player in eight, the LIVE combos (one per scene through `PathRenderer` and
  `InvariantChecker`) run as before, and `SWEEP_SAMPLE=1` runs every player. The GUT suite takes 29 s
  (78 s before). The precision sweep (`test_precision_invariant_sweep.gd`, through the live renderer) is
  sampled the same way.

## The goldens: judge, approve, re-bless

`bench/goldens/*.json` pin bit-identical behaviour (the traces of 284k sweep combos, the fixtures, the
corpus, the chains, the kernels, and `regions.json`: the region digest's 264 regions by hash). A change
there is a behaviour change, and the rule is: evidence first, the user's approval, then the re-bless.

1. **`make judge [REF=main] [RULES=stage106]`** (`tools/judge.sh`): a detached worktree of `REF` under
   `native/build-ref/` (gitignored, `.gdignore`d) builds `rc_tests` without the extension, cached by commit;
   the current `rc_tests` lists every changed sweep combo and golden field (`RC_SWEEP_REBLESS=1`,
   `RC_GOLDEN_REBLESS=1` → `native/build-ref/judge/rebless.txt`); both binaries dump those combos
   (`RC_SWEEP_DUMP`); `docs/research/judge.py` classifies them → `native/build-ref/judge/report.md`.
   Generic classes from the dumps alone: the planned aim (same, reversed, other), the first differing
   event of each trace, the cursor reached or lost, the tiers; a stage's rule module
   (`docs/research/<name>_judge.py`, `judge_trace(old, new, segs, portal_src)`) adds its own classes.
   Exit 0: nothing changed; 2: changes to show the user; 1: a rule's findings.
2. The user approves the report (or not).
3. **`make rebless APPROVED=1`** applies `rebless.txt` with `docs/research/apply_rebless.py`; commit the
   goldens with the change they pin. `make gates` passes the goldens gate again once they are committed.

The region digest's one-line summary (`make check TC="visibility digest" SKIP=0`) stays for the docs; the
pinned per-region hashes make the check automatic (`rc_tests_regions_{k}`, 6 parts, 7 s) and a change
prints the regions by name. A new level: `RC_REGIONS_WRITE=bench/goldens/regions.json rc_tests
-tc="visibility: the regions' digest*"` rewrites the whole golden.

## The fixtures and the corpus: `make regen`

`docs/research/regen.py` runs every `gen_fixtures_*.py` and `gen_scene_corpus.py` (timed), compares each
regenerated file with the committed one as parsed JSON (floats as floats, decimal strings as decimals, so
a different spelling of the same value is no change), prints the report (identical / byte-only /
changed cases per file / scenarios added and removed), and restores the files; `make regen-apply` keeps
only the files whose content changed, so a blanket regeneration is safe to commit. `ONLY=stage111`
narrows to one generator. On 2026-10-18 the 19 fixture files regenerate byte for byte and the corpus
takes 13 s. A changed expectation is a behaviour change: the oracle changed, and the native tests
(`test_corpus_oracle.cpp`, the fixture tests) say whether the kernel follows; bring it with the judge's
report.

`make dumps` rewrites `bench/scenes/*.surfaces.json` from the levels (Godot); the GUT parity tests fail
while they are stale, and the generators read them.

## The data pipelines

| Data | Producer | Target | Consumers |
|---|---|---|---|
| `bench/scenes/*.surfaces.json` (the levels as surface defs) | `scripts/testing/dump_scene_surfaces.gd` (Godot) | `make dumps` | the native visibility tests, the generators, `tscn_import.py`'s parity test |
| `bench/fixtures/*.json` | `docs/research/oracle/gen_fixtures_*.py` | `make regen` / `make regen-apply` | GUT and native fixture tests |
| `bench/scenes_corpus/{scenarios,expected}` | `docs/research/oracle/gen_scene_corpus.py` | `make regen` / `make regen-apply` | the GUT corpus test, `test_corpus_oracle.cpp`, the corpus goldens |
| `bench/goldens/*.json` | dumped at tag `pre-native` (the script is gone); re-blessed only | `make judge`, the user's approval, `make rebless APPROVED=1` | `test_goldens.cpp`, `test_visibility.cpp` (regions) |
| `bench/sweeps/*.json` | `SweepRunner.sweep_native` under `SWEEP_MANIFEST_DIR` | `make manifests` (the manifests gate fails while stale) | `test_sweeps.cpp` |

## A stage

1. Spec: `GAME_SPEC.md`, then the oracle and its tests (`make oracle-test`), then the fixtures.
2. Native: the kernel, its fixture tests and the goldens green under `make gate`.
3. Bindings, GUT, visuals.
4. Goldens judged, approved, re-blessed.
5. Docs: the TDD doc's phase table, CHANGELOG, GAME_SPEC's version.
6. Interactive sign-off on the behavioural phases.

`make gates` closes each phase; `make gate` precedes each commit. No stage is complete without the user's
sign-off on its interactive tests (CLAUDE.md).
