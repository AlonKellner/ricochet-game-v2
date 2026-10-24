# Element kinds and templates (`rc-level/2`)

An `rc-level/2` level (docs/LEVEL_FORMAT.md) is a list of **elements**: a wall, a mirror, a door, a button.
Every element kind is a **template**, which is data and never code. A template is a small JSON document that
expands an element (its geometry and parameters) into `rc-level/1` surfaces: their sides' path effects, hit
effects, resolvers, presence and player solidity. The builtin kinds are `data/elements/<kind>.json`, embedded
into the game when it is built. A level's own `templates` adds kinds or replaces builtin ones. A new element
can therefore be designed and played without touching the game's code, as long as it can be said with what a
surface already does (GAME_SPEC 9-10).

## The builtin kinds

| Kind | What it is | Parameters (defaults) | Label |
|---|---|---|---|
| `wall` | A solid barrier: the arrow stops at either side (terminal) and the player collides with it. Walls and most everyday surroundings. | -- | `Surface` |
| `arrow_block` | An arrow-block barrier (an energy field): stops the arrow at either side; the player passes. | -- | `ArrowBlock` |
| `fence` | An arrow-pass barrier (an iron fence): the arrow passes, the player is blocked. | -- | `Fence` |
| `passthrough` | A surface nothing collides with: the arrow passes (a step boundary, nothing else) and so does the player. A marker. | -- | `PassThrough` |
| `mirror` | A one-sided mirror: its left side (left of the walk start -> via -> end) reflects and is plannable; its back passes or blocks. `flip`: the mirror faces right. | `back`="pass" (or "block"), `solid`=false | `Mirror` |
| `two_sided_mirror` | A mirror reflecting on both sides, both plannable. | `solid`=false | `MirrorB` |
| `inversion` | A circle inversion (an arc or a full circle): both sides invert, or only the left one (the other passes; `flip` for the right). | `sides`="both" (or "left"), `solid`=true | `FullInversion` / `Inversion` |
| `projection` | A projection (GAME_SPEC 10.4) on both sides: the arrow leaves along the carrier's normal (line/circle normal) or along a fixed direction (directional: `normal`), forward or back. | `type`="line_normal_projection" (or circle_normal_, line_directional_, circle_directional_projection), `back`=false, `normal` (directional), `solid`=false | by type and back (`Projection`, `CircleDirBack`, ...) |
| `portal` | A portal pair: the surface and its image, the surface moved by z -> e^{i theta} z + d (two ids). The arrow entering one leaves the other. | `d` (required), `theta`=0.0, `solid`=false | `Portal` |
| `pre_inversion` | A pre-transform inversion circle (GAME_SPEC 9.5): never hit, never clicked; the level starts after it (the horizon). Built after every other surface. | -- | `PreTransform` |
| `ghost_gate` | A ghost toggler (GAME_SPEC 10.6, ghost mode): crossing it turns the arrow into a ghost that passes every surface until the next ghost gate. | -- | `GhostToggle` |
| `button` | A button: when the arrow hits it (either side), it sets or toggles a flag, usually a door's. | `flag`="door:${opens}", `opens` (a door's name), `op`="toggle" (or "set"), `value`=true, `deferred`=false, `face`="pass" (or "terminal", "reflection") | `Button` |
| `target` | A target: hitting it (either side) registers the flag target:<id> (GAME_SPEC 24). | `face`="terminal", `interactive`=true, `solid`=false | `Target` |
| `switchable` | A surface that switches with a flag (a categorical resolver, GAME_SPEC 9.4): its `on` sides while the flag is true, its `off` sides otherwise. Present either way (see `door` for one that disappears). | `flag` (required), `on`="pass", `off`="wall" (each "pass", "wall" or "mirror"), `solid`=false | `Gate` / `FlagMirror` |
| `door` | A door: a barrier that exists while its flag is unset or false, and is gone (for the arrow and the player) while it is true. A button opens it. | `flag`="door:${name}", `closed`="wall" (or "mirror", "fence"), `solid`=true | `Door` |
| `spawner` | A spawner (GAME_SPEC 10.6.1): when hit it places `surface` (an `rc-level/1` surface in a local frame, centred on the origin) at the hit when the arrow lands; it is gone until a resetter re-arms its channel. | `surface` (required), `channel`="${name}", `orientation`="CARRIER_TANGENT", `mirror_with_side`=false, `offset`=[0.0, 0.0], `trim`=true, `deferred`=false, `face`="pass" | `Spawner` |
| `resetter` | A resetter: when hit it re-arms spawn channels (their spawned surfaces go, their spawners come back). | `channels` (required), `deferred`=false, `face`="pass" | `Resetter` |

The format's own kinds are `edges`, which places the four edge walls where it stands, and `raw`, which takes an
`rc-level/1` surface as it is.

A door and the button that opens it:

```json
{"kind": "door", "name": "gate", "start": [900, 300], "end": [900, 500]},
{"kind": "button", "opens": "gate", "start": [600, 200], "end": [700, 200]}
```

## Derived flags

Flags are named after what writes or reads them, so levels and templates agree without ids:

| Flag | Written by | Read by |
|---|---|---|
| `door:<name>` | a button with `opens` | the door `<name>` (present while unset or false) |
| `target:<id>` | a target's side firing | the game (GAME_SPEC 24) |
| `chan:<channel>` | spawners and resetters | the spawner and its spawned surface (GAME_SPEC 10.6) |
| `ghost` | ghost gates | every side (the per-shot ghost mode) |
| `det:<name>` | the detector `<name>` (GAME_SPEC 24.5) | a door (`"flag": "det:plate"`), the goal's `requires` |

The level's `flags` sets any of them at the start. For example, `{"door:gate": true}` starts with the door open.

## The template language

A template is an object:

| Key | Value |
|---|---|
| `doc` | what the element is (the editor shows it) |
| `label` | the surfaces' label (node prefix), an expression; the element's `label` overrides it |
| `priority` | the order in which `rc_tool migrate` tries the kinds (lower first; default 100) |
| `geometry` | `"any"` (default), `"segment"` or `"circle"`: what the element needs (the editor's tools) |
| `params` | `{name: {"default": v, "enum": [...], "type": t, "doc": "..."}}`; without a default a parameter is required; `type` is one of string, number, bool, point, array, object, any (by default the default's) |
| `example` | parameter values a new element starts with (the editor's palette: a portal placed with `d` already set) |
| `surfaces` | the surfaces, as `rc-level/1` surfaces without geometry, ids or (optionally) label; expressions anywhere |

Expressions:
- `"$p"` is the value of parameter `p`, of any JSON type: `"player_solid": "$solid"`.
- `"text ${p} text"` puts the value of `p` inside a string: `"key": "door:${name}"`. `name` is the element's name.
- `{"$switch": "p", "cases": {"a": ..., "b": ...}}` is the case named by the spelling of `p`'s value (a string
  itself; `true`/`false`; a number's shortest decimal).

A parameter's default may refer to other parameters (`"flag": {"default": "door:${opens}"}`). It is then
required whenever what it refers to is unset. Every surface of an element shares the element's geometry
(several surfaces stack on one carrier), and their ids follow in order. `flip` swaps every surface's left and
right sides, its resolver's too.

What a template cannot say: arithmetic, geometry of its own (offsets, a second shape), conditions beyond
`$switch`, loops, a new hit-effect kind or a new path effect. Those need a primitive in the game. The editor
program adds four of them: the goal objective, detectors, bodies, and grapple and pull.

## Designing a new element

1. Write the template into the level's `templates`, with the kind name you want, and use it in `elements`:

   ```json
   "templates": {"glass": {"doc": "Lets the arrow through, stops the player; dark glass stops the arrow too.",
     "label": "Glass", "params": {"tint": {"default": "clear", "enum": ["clear", "dark"]}},
     "surfaces": [{"left": {"$switch": "tint", "cases": {
                     "clear": {"effect": {"type": "pass"}, "interactive": false},
                     "dark": {"effect": {"type": "terminal"}, "interactive": false}}},
                   "player_solid": true,
                   "right": {"$switch": "tint", "cases": {
                     "clear": {"effect": {"type": "pass"}, "interactive": false},
                     "dark": {"effect": {"type": "terminal"}, "interactive": false}}}}]}},
   "elements": [{"kind": "glass", "tint": "dark", "start": [800, 300], "end": [800, 600]}]
   ```

2. Play it: any host reads the level (`rc_tool fmt --check` reports a mistake with the element's path).
3. To make it a builtin kind, move the template to `data/elements/<kind>.json` and rebuild (`make core`); give
   it a `priority` if `rc_tool migrate` should recognise it. The `rc-level/2` tests check that every builtin
   kind expands and builds.

## Migration

`rc_tool migrate` (docs/LEVEL_FORMAT.md) runs the templates backwards. For each run of surfaces it tries the
kinds by priority, binding the parameters the run fixes. A `$switch` tries the default's case first, so the
parameters left at their defaults are not written. A label that differs from the template's becomes the
element's `label`. The result is kept only if it expands back to the run exactly. The 37 levels of Stage 142
became 132 elements, 3 of them `raw` (in `test_levels/hit_effects`: a spawning portal, a target that also
toggles and spawns, and a resolver whose open state spawns).
