# Ricochet Game v2

## Stack

- **Engine**: Godot 4.7+ (GDScript only)
- **Testing**: GUT 9.6.0 (Godot Unit Test)
- **IDE**: VSCode

## Architecture

Three-layer architecture with strict dependency direction: Math -> Visual -> Game.

- `scripts/math/` — Geometric primitives and algorithms. **Zero Godot dependencies beyond Vector2.** Classes must extend `RefCounted`, `Object`, or have no `extends`. Never `Node`, `Node2D`, `Control`, or any scene-tree type.
- `scripts/math/effects/` — Effect implementations (reflection, inversion, rigid motion, projective).
- `scripts/visual/` — Rendering via `_draw()` and `queue_redraw()`.
- `scripts/game/` — Scene-dependent game logic (player, surfaces, game manager).
- `scripts/editor/` — Level editor scripts.

## Testing

```bash
# Run all tests headlessly
./run_tests.sh

# Or directly
godot --headless -s addons/gut/gut_cmdln.gd

# Run specific stage
make test-stage STAGE=3
```

- Test files: `tests/test_stage{N}_{topic}.gd`
- All tests extend `GutTest`
- Test methods: `func test_{description}():`
- **Regression policy**: After Stage N, ALL tests from Stages 1 through N must pass before proceeding.

## Stage Workflow

Each stage follows the feedback loop protocol:

1. Implement stage (code + unit tests)
2. Run all unit tests (new + all prior) — all must pass
3. User performs interactive tests
4. User provides pass/fail feedback
5. Convert failing interactive tests into automated tests
6. Fix and re-run — repeat until all pass
7. Stage complete

**No stage is complete without user sign-off on interactive tests.**

## Code Style

- GDScript conventions, `snake_case` for variables/functions, `PascalCase` for classes
- Type hints where possible
- Immutable data classes extend `RefCounted`
- Error handling: debug builds assert+crash on invariant violations; release builds log+recover
- Complex numbers represented as `Vector2(real, imag)`
- Coordinate system: Y-down, X-right (Godot default), 1 unit = 1 pixel

## Key References

- `GAME_SPEC.md` — Single source of truth for game design (25 core principles in section 2)
- `docs/TDD_01_FOUNDATION.md` through `docs/TDD_08_EDITOR_AND_POLISH.md` — Stage-by-stage implementation guide
