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
| 119.2 | The session ports: new session scripts, recorded by the GDScript game and replayed natively | |
| 119.3 | The unit ports: doctests | |
| 119.4 | The switch: the scenes on the adapter; the GDScript game, `native/gd` and the retired GUT files deleted; the final Godot suite; the native recorder and frame bench | |
| 119.5 | Gates and lints: `SKIP_GODOT=1 make gate`, `check_no_engine`, the GDScript budget, the ledger check | |
| 119.6 | `engines/godot/`; `docs/PORTING.md`; docs (GAME_SPEC, CLAUDE.md, WORKFLOW, CHANGELOG); `make gates` | |
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
