# Element kinds and templates (`rc-level/2`)

An `rc-level/2` level (docs/LEVEL_FORMAT.md) is a list of **elements**: a wall, a mirror, a door, a button.
Every element kind is data, never code: a **template**, or a **preset** of the surface element (Stage 207). A template is a small JSON document that
expands an element (its geometry and parameters) into `rc-level/1` surfaces: their sides' path effects, hit
effects, resolvers, presence and player solidity. The builtin kinds are `data/elements/<kind>.json`, embedded
into the game when it is built. A level's own `templates` adds kinds or replaces builtin ones. A new element
can therefore be designed and played without touching the game's code, as long as it can be said with what a
surface already does (GAME_SPEC 9-10).

## The surface element and its presets (Stage 207)

Most surfaces are one element, the **surface element** (GAME_SPEC 9.13): its properties are separate keys, so a
surface may block the player whatever it does to the arrow, and carry any of the features.

| Key | Values (default) | What it is |
|---|---|---|
| `face` | `stop`, `pass`, `mirror` (`stop`) | what the arrow does: the left side's, and both sides' unless `back` |
| `back` | `stop`, `pass`, `mirror` (unset: the face) | the right side's face |
| `body` | `pass`, `block`, `platform` (`block`) | what the player and objects do: pass it, collide with it, stand on it from above |
| `plannable` | true, false (unset: a side that reflects or has a hit) | its sides can be planned |
| `flag`, `opens`, `op`, `value`, `deferred` | `op`="toggle" (or "set"), `value`=true, `deferred`=false | **button**: a hit writes `flag` (`opens`: the door's, `door:<opens>`); deferred: when the arrow lands |
| `target` | false | **target**: a hit registers `target:<id>` |
| `grapple` | false | **grapple**: a hit pulls the player to it when the shot lands |
| `spawn`, `channel`, `orientation`, `mirror_with_side`, `offset`, `trim` | `channel`="${name}", `orientation`="CARRIER_TANGENT" (or BEAM_TANGENT, BEAM_NORMAL, CARRIER_NORMAL, BEAM_REFLECTED, WORLD), `mirror_with_side`=false, `offset`=[0, 0], `trim`=true | **spawn**: a hit places `spawn` (a surface in a local frame) at the hit |
| `channels` | -- | **reset**: a hit re-arms these spawn channels |
| `breaks` | false | **break**: hit, it is gone for the rest of the shot (`shot:<name>:broken`) |
| `ghost` | false | **ghost**: a hit toggles the arrow's ghost mode |
| `open` | a flag expression | **door**: the surface exists while it is unset or false |

The hits compose in the table's order on both sides, and the presence conditions (all required) are the door's, then
the break's. **Its look** follows from its face and body: the face's colour, a steel band under a surface that blocks
but does not stop (a fence, a solid mirror, a blocking button), turquoise for a shield, wood brown for a platform.

The kinds below are **presets**: a label and defaults (`data/elements/<kind>.json`: `{"extends": "surface",
"defaults": {...}, "features": [...]}`; `features`: the features the kind is about, which it requires and the editor
shows). Every preset takes every key, for example a button the player stands on:

```json
{"kind": "button", "opens": "gate", "body": "block", "start": [600, 200], "end": [700, 200]}
```

| Kind | What it is | Defaults | Label |
|---|---|---|---|
| `wall` | A wall: the arrow stops at either side and the player and objects collide with it. Walls and most everyday surroundings. | -- | `Surface` |
| `shield` | A shield (an energy field): stops the arrow; the player and objects pass. Drawn turquoise in a band, unlike a wall. | `body`="pass" | `Shield` |
| `fence` | An arrow-pass barrier (an iron fence): the arrow passes, the player and objects are blocked. Drawn over a steel band. | `face`="pass" | `Fence` |
| `passthrough` | A surface nothing collides with: the arrow passes as if it were not there (inert, GAME_SPEC 9.7) and so does the player. A marker. | `face`="pass", `body`="pass" | `PassThrough` |
| `platform` | A one-way platform (GAME_SPEC 25.1): the arrow passes; the player and objects stand on its top and pass it from below or the side; the player holding down drops through; moving in any direction while overlapping it, the player engages it (gravity-free movement); a pulled object or a grappled player passes it. Drawn wood brown. Unlike a `fence`, which blocks the player every way and stops a pull. | `face`="pass", `body`="platform" | `Platform` |
| `mirror` | A mirror reflecting on both sides, both plannable. | `face`="mirror", `body`="pass" | `Mirror` |
| `one_sided_mirror` | A one-sided mirror: its left side (left of the walk start -> via -> end) reflects and is plannable; its back passes (or stops: `back`="stop"). `flip`: the mirror faces right. | `face`="mirror", `back`="pass", `body`="pass" | `OneSidedMirror` |
| `button` | A button: when the arrow hits it (either side), it sets or toggles a flag, usually a door's (`opens`: the door's name). `body`="block": one the player stands on and objects rest on. | `face`="pass", `body`="pass"; requires `flag` or `opens` | `Button` |
| `target` | A target: hitting it (either side) registers the flag target:<id> (GAME_SPEC 24). | `target`=true, `body`="pass" | `Target` |
| `door` | A door: a barrier that exists while its flag expression `open` is unset or false, and is gone (for the arrow and the player) while it is true. Drawn with amber posts across its ends and an amber core -- as is any surface given an `open` (Stage 213). A button opens it (`opens`: its name). | `open`="door:${name}" | `Door` |
| `switchable` | A surface that switches with a flag (a categorical resolver, GAME_SPEC 9.4): its `then_face` (and `then_back`) sides while the flag expression `when` holds, its `face` (and `back`) otherwise. Present either way (see `door` for one that disappears). Stage 214: a preset (the switch feature). | `when` (required), `face`="stop", `then_face`="pass", `body`="pass" | `Gate` / `FlagMirror` (by `then_face`) |
| `conditional_button` | A button only a charged (or burning, frosted...) arrow presses: while `armed` holds the hit sets or toggles `flag`; otherwise it is a wall. Stage 214: a preset (a button, armed). | `armed`="shot:charge", `flag` (required), `op`="set", `value`=true, `face`="stop", `body`="pass" | `CondButton` |
| `grapple` | A grappling surface: when the arrow hits it, the player is pulled along the arrow's path to the hit (when the shot lands), stopping at whatever blocks the player first (a fence does; a shield does not). | `grapple`=true | `Grapple` |
| `breakable` | A breakable surface: hit, it does what its face does, then it is gone for the rest of the shot; it is back when the arrow lands. Flag `shot:<name>:broken`. | `breaks`=true | `Breakable` |
| `spawner` | A spawner (GAME_SPEC 10.6.1): when hit it places `spawn` (an `rc-level/1` surface in a local frame, centred on the origin) at the hit when the arrow lands; it is gone until a resetter re-arms its channel. | `face`="pass", `body`="pass"; requires `spawn` | `Spawner` |
| `resetter` | A resetter: when hit it re-arms spawn channels (their spawned surfaces go, their spawners come back). | `face`="pass", `body`="pass"; requires `channels` | `Resetter` |
| `ghost_gate` | A ghost toggler (GAME_SPEC 10.6, ghost mode): crossing it turns the arrow into a ghost that passes every surface until the next ghost gate. | `face`="pass", `body`="pass", `ghost`=true | `GhostToggle` |

## The templates

The other kinds are templates (below): what they do is more than the surface element says (a circle's inversion, a
projection, a portal pair, the kit of arrow states). Each takes `body` (`pass` | `block`) for
its solidity.

| Kind | What it is | Parameters (defaults) | Label |
|---|---|---|---|
| `inversion` | A circle inversion (an arc or a full circle): both sides invert, or only the left one (`back`: `pass`; `flip` for the right). | `back`="invert" (or "pass"), `body`="block" | `FullInversion` / `Inversion` |
| `projection` | A projection (GAME_SPEC 10.4) on both sides: the arrow leaves along the carrier's normal (line/circle normal) or along a fixed direction (directional: `normal`), forward or back (`reverse`). | `type`="line_normal_projection" (or circle_normal_, line_directional_, circle_directional_projection), `reverse`=false, `normal` (directional), `body`="pass" | by type and reverse (`Projection`, `CircleDirBack`, ...) |
| `portal` | A portal pair: the surface and its image, the surface moved by z -> e^{i theta} z + d (two ids). The arrow entering one leaves the other. | `d` (required), `theta`=0.0, `body`="pass" | `Portal` |
| `pre_inversion` | A pre-transform inversion circle (GAME_SPEC 9.5): never hit, never clicked; the level starts after it (the horizon). Built after every other surface. | -- | `PreTransform` |
| `frost_barrier` | A frost barrier (tag `frosted`): an arrow passing it is frosted (`shot:frost`); the barrier stays. A frosted arrow that would be stopped is reflected (a line) or inverted (a circle) instead, once. | `body`="pass" | `Frost` |
| `torch` | A torch: a pass-through surface on fire (tag `burning`). An arrow passing it catches fire (`shot:fire`). | `body`="pass" | `Torch` |
| `water` | Water: an extinguishing barrier. An arrow passing it is put out (`shot:fire` = false); the player passes it too. | `fire`="shot:fire", `body`="pass" | `Water` |
| `burnable` | A burnable barrier (tag `flammable`): solid, but an arrow on fire burns through it. It catches fire (`el:<name>:fire`) and is gone from then on. | `body`="block" | `Burnable` |
| `charger` | A charger: an endless source. An arrow passing it is charged (`shot:charge`); the charger stays charged. | `charge`="shot:charge", `body`="pass" | `Charger` |
| `conductor` | A conductor (tag `conductive`): touching it, the arrow and it swap their charge (`shot:charge`, `el:<name>:charge`). | `face`="pass" (or "stop"), `body`="block" | `Conductor` |
| `battery` | A battery (tags `conductive`, `charged`, `powered`): a conductor that starts charged. An uncharged arrow touching it takes the charge. | `body`="block" | `Battery` |
| `charged_door` | A charged door (tag `conductive`): a wall that is gone while charged (`el:<name>:charge`). A charged arrow gives it the charge and opens it. | `body`="block" | `ChargedDoor` |
| `dark` | A dark barrier: one side of its line (or the inside or outside of its circle) is a dark region (GAME_SPEC 9.9). Its surface is never present: the shadow's boundary is the game's. | `side`="auto" (left, inside), `when`="" (always) | none (drawn by the shadow) |
| `light` | A light region: one side of its line, the inside of its circle (a light source's reach) or its outside. Light undoes the shadow wherever it reaches. | `side`="auto", `when`="" | none |

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
| `door:<name>` | a button with `opens` | the door `<name>` (its `open`: present while unset or false) |
| `target:<id>` | a target's side firing | the game (GAME_SPEC 24) |
| `chan:<channel>` | spawners and resetters | the spawner and its spawned surface (GAME_SPEC 10.6) |
| `ghost` | ghost gates | every side (the per-shot ghost mode) |
| `det:<name>` | the detector `<name>` (GAME_SPEC 24.5) | a door (`"open": "det:plate"`), the goal's `requires` |
| `shot:*` | anything in a shot: `shot:fire`, `shot:frost`, `shot:charge`, `shot:<name>:broken` | erased when the arrow lands (GAME_SPEC 10.6): a state of the arrow, not of the world |
| `el:<name>:*` | an element's own state: `el:<name>:off`, `el:<name>:burnt` | the element (kept across shots) |
| `el:<name>:fire`, `el:<name>:charge` | the arrow and the element's or object's tags (Stage 156), or a spread rule | the element: `burnable`, `charged_door` |

The level's `flags` sets any of them at the start. For example, `{"door:gate": true}` starts with the door open.

**Flag expressions** (Stage 171, GAME_SPEC 9.11). A parameter that reads a flag -- a door's `open`, a switchable's
`flag`, a conditional button's condition -- may combine flags: `!` (not), `^` (exclusive or), `&&` (and), `||` (or)
and parentheses, with C's precedence (`!`, then `^`, then `&&`, then `||`). A missing flag is false. For example:

```json
{"kind": "door", "name": "trap", "start": [960, 240], "end": [960, 840], "open": "!det:plate"}
{"kind": "door", "name": "vault", "start": [1100, 240], "end": [1100, 840], "open": "(det:red && det:blue) || det:key"}
```

The first is closed while the player stands on the plate (a `while` detector) and open otherwise; the second opens
when both plates are pressed, or with the key. A parameter that writes a flag (a button's `flag`) takes a name only.

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
| `tags` | the kind's tags (`flammable`, `conductive`, `frosted`, `burning`, `charged`, or any name a spread rule uses) |
| `shade` | `{"type": "dark" \| "light", "side", "when"}` (expressions): the element bounds a region of that kind (GAME_SPEC 9.9) |
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
program adds four of them: the goal objective, detectors, bodies, and grapple and pull; Stage 172 adds the
platform (the surface key `platform`).

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

## The kit: elements that remember, and arrows that carry

Two kinds of flags make most novel elements templates:
- **A per-shot flag** (`shot:*`) is a state of the arrow: set when the arrow passes something, read by what it
  meets later in the same shot, and erased when it lands. Fire, frost and charge are per-shot flags.
- **An element's flag** (`el:<name>:*`) is a state of the world, kept across shots (and undone by undo).

They combine with presence (a surface that exists only under a flag), resolvers (a surface whose sides depend on a
flag) and state writes on hits:

| Pattern | How | Examples |
|---|---|---|
| the arrow picks up a property | a pass-through side writing `shot:<p>` = true | `torch`, `frost_barrier`, `charger` |
| it loses it | a pass-through side writing false | `water` |
| a surface reacts only to it | a resolver on `shot:<p>`: the default row the plain surface, the `true` row the reaction (the surface element's `switch` or `armed`, Stage 214) | `conditional_button`, `burnable`, any wall (a frosted arrow) |
| a reaction that uses it up | the reaction's side writes `shot:<p>` = false | a wall a frosted arrow reflects off (one hit) |
| gone for the shot | presence `[{"key": "shot:<name>:x", "values": ["DEFAULT"]}]` and a hit setting it | `breakable` |
| gone for good | the same with `el:<name>:x` | `burnable` |
| a state that comes back | two surfaces on one geometry, each present in one state, each writing the other | (the toggle surface, retired in Stage 214: no level used it) |

What the kit cannot say needs a primitive. Moving surfaces have one: the level's `cycle` and an element's `motion`
(GAME_SPEC 9.8, docs/TDD_56_MOTION.md); surfaces that move when a flag holds another: an opening, the same `motion` with `"motion_mode": "opening"` (a door
that swings about a hinge, a gate that slides, a platform a button sends across; GAME_SPEC 9.12,
docs/TDD_80_OPENINGS.md; a step or a key by two points, Stage 203). A `door` vanishes for the arrow that opens it; an opening moves once the shot lands. Spreading has one: tags and the level's `spread` rules (below). Light and
shadow have one: a template's `shade` (below).

## Fire, charge and frost: tags

An element or an object carries `tags` (a template's `tags`, plus the element's own). Five tags give it a part in the
arrow's states (GAME_SPEC 10.6, Stage 156). The arrow, a surface and an object are each on fire or not, charged or
not; the arrow is frosted or not. The arrow's states last one shot (`shot:fire`, `shot:charge`, `shot:frost`); a
thing's last (`el:<name>:fire`, `el:<name>:charge`).

| Tag | What every side of it does when the arrow touches it |
|---|---|
| `flammable` | an arrow on fire sets it on fire; an arrow not on fire touching it while it burns catches fire |
| `conductive` | the arrow and it swap their charge (both charged, or neither: nothing) |
| `frosted` | the arrow is frosted (its sides must pass, reflect or invert) |
| `burning` | `flammable`, on fire from the start |
| `charged` | `conductive`, charged from the start |

The tags become conditional state writes on the element's sides (`"when"`, see the template language), so the solver
plays them out within the shot: a burning arrow lights a wall, the next arrow catches fire from it. A pass-through
conductive circle is touched twice by an arrow crossing it, entering and leaving, so a charge given is taken back.

Frost needs no tag on the walls: a frosted arrow that a side would stop (a terminal) is reflected by a line or
inverted by a circle instead, and the frost is used up. A mirror or a pass keeps the frost. The shadow's edge is
rigid: frost does not turn it. While a planned frost is active (a frost source planned, its frost not yet used up),
every wall and other stopping side is plannable as that reflection. A shot exactly at a corner (a shared point) stops
the arrow, frost or not.

**Touching rules (opt-in).** A level's `spread` rules flow a flag through tagged things that touch (within half a
pixel), with no arrow: an element by its segment or arc, an object by its circle.

```json
"spread": [{"flag": "fire", "tag": "flammable", "persist": true},
           {"flag": "charge", "tag": "conductive", "persist": false, "sources": ["powered"]}]
```

| Key | Meaning |
|---|---|
| `flag` | the flag written, per thing: `el:<name>:<flag>` = true |
| `tag` | what carries it |
| `persist` | `true`: once lit, lit for good, and it flows through every tagged thing, present or not (fire). `false`: recomputed whenever the world changes; it flows only through present things, and an absent thing it reaches is lit but passes nothing on (charge: an open door is charged, so it stays open, yet breaks the circuit) |
| `sources` | tags lit by themselves (a battery is `powered`); without sources only things already lit spread |

A level has no touching rules unless it names them (since Stage 156). A recomputed rule overrides what the arrow did
to the things it covers. The demo `objectives/circuit` uses the charge rule: a box dropped across a gap closes the
circuit.

## Light and shadow

A template with a `shade` makes its element bound a region (GAME_SPEC 9.9): `dark` or `light`, on one `side` of
the element's line (`left`, `right`, as the editor's side marks show them), or `inside` or `outside` its circle (an
arc: its circle's), active while the flag `when` is true (empty: always). A point is in the **shadow** when it is
in an active dark region and in no active light one.

- The arrow stops where it would enter the shadow: the shadow's edge is a wall from the light and a pass from the
  dark. An arrow already in the shadow (fired from it, or through a portal) passes out.
- A surface lying wholly in the shadow is gone for the arrow (a pass, no hits). It stays solid for the player and is
  drawn under the shadow. One the shadow's edge crosses stays whole.
- Regions follow their carriers: a line's region is a half-plane, clipped to the bounds; a circle's is its disk or
  the rest of the plane.

```json
{"kind": "dark", "name": "night", "side": "left", "start": [1000, 240], "end": [1000, 840]},
{"kind": "light", "name": "lamp", "when": "lamp", "circle": {"center": [1250, 500], "radius": 320}},
{"kind": "button", "name": "switch", "flag": "lamp", "start": [800, 300], "end": [900, 300]}
```

A light under a flag is a lamp: switched by a button, a detector or charge (`"when": "el:wire:charge"`). The
shadow follows the flags when a shot lands and as the player moves. A shot sees the shadow of its moment. A shade
does not move (v1).

## Migration

`rc_tool migrate` (docs/LEVEL_FORMAT.md) runs the templates backwards. For each run of surfaces it tries the
kinds by priority, binding the parameters the run fixes. A `$switch` tries the default's case first, so the
parameters left at their defaults are not written. A label that differs from the template's becomes the
element's `label`. The result is kept only if it expands back to the run exactly. The 37 levels of Stage 142
became 132 elements, 3 of them `raw` (in `test_levels/hit_effects`: a spawning portal, a target that also
toggles and spawns, and a resolver whose open state spawns).
