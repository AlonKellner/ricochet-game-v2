# TDD 24: The switch -- the GUT suite retired, Godot a thin host (Stage 119)

The last stage of the engine-agnostic port (TDD_20's introduction). The Godot scenes switch to the adapter
(`RicochetGame`, TDD_23), and the following are deleted:

- the GDScript game;
- its GDExtension bindings (`native/gd`);
- the GUT suite.

What the GUT suite asserted moves to native tests, fixtures and sessions, as a ledger records row by row.
Godot then moves to `engines/godot/`, and `docs/PORTING.md` documents the engine contract.

Background: TDD_20–TDD_23.

## Phase table

| Phase | Topic | Status |
|---|---|---|
| 119.0 | Branch `agnostic-switch`, this doc; the ledger (`docs/research/gut_ledger.tsv`, approved by the user 2026-10-20 with the plan below); the dev tools decision (the recorder and the frame bench ported, the precision bench and the violation viewer dropped) | Done |
| 119.1 | The captures: the GUT solver scenarios recorded and replayed natively | Done |
| 119.2 | The session ports: new session scripts, recorded by the GDScript game and replayed natively | Done |
| 119.3 | The unit ports: doctests | Done |
| 119.4 | The switch: the scenes on the adapter; the GDScript game, `native/gd` and the retired GUT files deleted; the final Godot suite; the native recorder and frame bench | Done |
| 119.5 | Gates and lints: `SKIP_GODOT=1 make gate`, `check_no_engine`, the GDScript budget, the ledger check | Done |
| 119.6 | `engines/godot/`; `docs/PORTING.md`; docs (GAME_SPEC 2.31.0, CLAUDE.md, WORKFLOW, CHANGELOG); `make gates` (938 s, green) | Done |
| 119.7 | Interactive sign-off: the game from `engines/godot/` | |

### 119.0 The ledger

`docs/research/gut_ledger.tsv` has one row per GUT test function: 862 rows, covering 120 files and the 12
extension-load tests.

**How the rows were classified.** Six survey agents read each test against the native tests, fixtures,
goldens and the session golden. Each row gets one disposition:

| Disposition | Rows | Meaning |
|---|---|---|
| covered | 353 | an existing native test, golden, fixture, oracle test or session asserts it; the citation is in the row |
| port | 254 | a doctest or a new session script to write |
| capture | 125 | a solver scenario recorded from the GUT run, replayed natively |
| keep | 9 | stays in the Godot suite (the adapter) |
| drop | 121 | dropped, with a reason: GDScript class internals and API shape, tautologies, duplicates, legacy-absence scans, differential probes whose purpose ends with the GDScript game |

**The native column.** The last column says where each row lives now. `tools/gut_ledger_check.py` checks it:

- a capture's key is in its fixture;
- a port's target exists: a TEST_CASE, a session script or a host_py test;
- a keep's target exists;
- every GUT test still in `tests/` is in the ledger or in the final suite.

### 119.1 The captures

A capture mode records each scenario.

- **The hook.** `native/gd/gd_tracer.cpp`'s `BeamTracer.trace_pair`, with `RC_CAPTURE` and
  `RC_CAPTURE_KEY` set, appends every call:
  - its inputs as the solver takes them: the scene defs, player, cursor, plan, flags and the infinity switch;
  - its output: both paths with every step, hit record and target; the final flags; the cursor agreement;
    and whether the pair is shared.
  
  The format is `rcg/trace_json.hpp`. The output is held in the digest encoding, because a step's end at
  infinity is `inf` and JSON has no `inf`.
- **The driver.** `tools/capture_gut_traces.py` runs each capture test alone in Godot. A test that fails
  records nothing.
- **The crossings probe.** `test_stage116_geom_probe.gd`'s random crossings probe (3000 pairs) records each
  pair and the GDScript game's answers itself.
- **The replay.** The 125 tests give 26 files in `bench/gut_traces` (8.6 MB): 600 trace calls and 3000
  crossing pairs. The `rcg_tests` case "gut traces: the GUT suite's solver scenarios replay bit for bit"
  replays them:
  - each call on `rc::Tracer`, with the defs read back exactly as written;
  - each pair on `rcg::crossings_of`, `param`, `incident` and `span`.
  
  Everything is equal bit for bit.

A capture pins the scenario's whole output, which is stricter than what the GUT assertion checked. A
behaviour change shows up as a fixture change. The ledger row keeps what the test asserted, so the change
can be judged against it.

### 119.2 The session ports

`docs/research/sessions/ported_sessions.py` writes 8 session scripts (`*__ported_*`), the interactions of the
GUT tests that played the game:

| Session | What it plays |
|---|---|
| `main` | undo after moving; the cursor on the player (no preview, no shot); the camera after a flight, an escaping one too |
| `pre_inverted` | Q0 on screen, hovered, planned, the HUD's "∞"; the unplanned passage; ∞ first |
| `open_room_playable` | the edge band (10 px in, not 40 px in, not outside); right clicks removing the latest ∞; the switch off |
| `ghost_room` | refusal inside a ghost stretch |
| `hit_effects` | a host armed, a target registered, a gate opened, a reset re-arming |
| `crossings` | a spawn trimmed by the floor |
| `flag_switches` | a landing that changes the flags |

**Evidence.** The GDScript game recorded them, and the native replay is byte-identical. They joined the
golden as approved (48 sessions).

**Native tests for the rest.** The plan overlays' builder was extracted as `rcg::plan_overlays` (no behaviour
change: the golden replays unchanged), and `Session::visibility_version` / `preview_version` count the
recomputations. With those, the plan-highlight and recompute tests became doctests.

### 119.3 The unit ports

Five agents ported the remaining rows, each in its own worktree, writing new files only:

- **Doctest files:** `native/tests/test_ported_{visual,effects,tracer}.cpp` and
  `native/tests/game/test_ported_{game,session_units,effects_game,tracer_game,overlays}.cpp`.
- **Python:** `tests/host_py/test_ported_flight.py`.
- **Naming:** one TEST_CASE per GUT function, "ported: <gut file>: <function>".

Every row ported; none needed a source change beyond 119.2's.

**A new oracle comparison.** "ported: the stage 81-87 fixtures match the oracle" compares all 115 fixtures with
the oracle natively, at f64, Dual, 4 and 64 limbs. No native test did this before; no fixture disagreed.

**Where a port asserts less, or differently, than its GUT test:**

- **B7 snap (`test_b7_...`):** 4 of 6 assertions. Through `click()`, two of the clicks fall outside the
  click tolerance, and `snap_click` is private.
- **Spawn preview equals the landing (`test_stage107_preview_equals_landing`):** compared through the spawn
  preview's drawn outlines against the surfaces that land and leave. The placed specs are private.
- **Random inputs (stage 81, 82 and 87 effects):** the same seeds through `std::mt19937_64`, not Godot's RNG.
  The same properties hold over other samples.
- **Clicks during a flight (`test_stage22_plan_blocked_during_flight`):** the GUT test's click hit no
  surface. The port also clicks a mirror, during the flight and after it.

The ledger's port column names every target. `tools/gut_ledger_check.py` (ctest `gut_ledger`) checks them.

### 119.4 The switch

**The Godot suite.** `engines/godot/tests/test_adapter_smoke.gd` is Stage 118's smoke test plus the ledger's
keep rows:

- the engine version;
- errors reaching Godot;
- the camera's smoothing and limits;
- no leaked objects over every level.

`test_session_replay.gd` plays one teleport session in 4 through Godot's InputEvents: clicks and keys reach
`RicochetGame._input`. Every record equals the golden.

The adapter gained three hooks for this, and two fixes:

- **Hooks:** `aim`, `teleport` and `physics`.
- **A clear error for a missing level:** one message, instead of FileAccess's.
- **The physics switch:** it gates only the player's ticks. Before the fix, a flight with the physics off never
  landed.

**What was deleted:**

- the retired GUT files (117) and `tests/native`;
- the GDScript game, math and visual layers;
- the testing scripts: the recorder, the precision and frame benches, the violation viewer, the shots tool,
  the capture driver;
- `native/gd`, with the BeamTracer capture.

The extension is `native/hosts/godot`, with its own `register_types.cpp`, linked to `rc_capi` alone.

**The scenes.** Every level scene is one `RicochetGame` node with its `level_path`; the uids were kept.

**The tools.** `make sessions` / `sessions-check` are `rc_tool replay`. `make frame-bench` is `rc_tool
bench`: phases of still and moving frames. A still frame never recomputes, and the mean frame stays under
8 ms (the player's orbit is reported only).

### 119.5 Gates and lints

**The lints (ctest):**

- `*_no_engine` (`check_no_engine.cmake`): core, game, the C API and the headless host name no engine:
  Godot, Unity or Unreal.
- `godot_adapter_includes`.
- `gdscript_budget`: 86 lines outside tests and addons, against a budget of 300.
- `gut_ledger`.
- `godot_api_matches_binary` (renamed from `rc_api_matches_binary`).

**The gates.** `SKIP_GODOT=1 make gate` runs every engine-free gate (156 s). The `sessions` gate was retired:
ctest and the suites cover the session golden.

### 119.6 engines/godot

**What moved.** The Godot project moved to `engines/godot`: `project.godot`, `override.cfg`,
`.gutconfig.json`, `ricochet.gdextension`, the scenes, the engine-reference recorders, `addons/gut`, the
host's tests and the TLS bundle.

**The build.**

- The extension builds into `engines/godot/bin`.
- `make native` copies `data/levels`, `data/sessions` and the session golden into `engines/godot/data`. This
  is gitignored; Godot reads only `res://`.

**The docs.**

- `docs/PORTING.md`;
- GAME_SPEC 2.31.0: the engine (line 5), the layers (§20.1), the structure (§20.2), the saves (§27), the
  testing (§29);
- CLAUDE.md, WORKFLOW, session_digest.md, CHANGELOG.

## Findings

| # | Finding | Status |
|---|---|---|
| F5 (TDD_23) | The player and cursor draw under the surfaces (GAME_SPEC said z 40) | Documented, unchanged |
| F6 | The native session computes the visibility region inside the frame. The GDScript game computed it on a worker thread. `rc_tool bench`, per frame while the player moves: main 0.8 ms mean; crossings 15.4 ms mean, 22.6 ms max. The plan's asynchronous visibility job (`rcg::VisibilityJob`, a sync switch for replays) was not built in Stage 118. | Open: for the user to decide (a follow-up stage) |
