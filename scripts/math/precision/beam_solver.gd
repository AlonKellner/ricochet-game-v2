class_name BeamSolver
extends RefCounted

# Precision-generic trajectory solver for scenes of line / arc / full-circle
# surfaces with reflection, inversion, terminal and pass sides (BeamScene).
# The trace rules are those of the mpmath oracle (docs/research/oracle/
# trace2.py), which is the specification:
#   - every carrier crossing is an event; MAX_HITS = 32 counts every event
#     (cursor and the final origin/escape event too); a trace cut by MAX_HITS
#     ends with a "max" event at the next crossing (drawn up to it);
#   - PHYSICAL: an effect fires only on a fully blocked hit (on-segment
#     interior, or an exact endpoint whose blocked sides, including other
#     segments sharing that endpoint, cover both sides); terminal ends the
#     trace; plan bookkeeping by surface id;
#   - PLANNED: the next plan entry fires on an id AND side match, even
#     off-segment; everything else passes (terminals included);
#   - after applying at h on S the new beam departs S at h; the departure point
#     is re-hit last (after the full loop), as an ordinary hit;
#   - provenance stack: re-applying the surface on top of the stack pops it,
#     restoring the earlier beam object exactly, resumed at the shared point
#     actually hit;
#   - projective order from the current parameter; ties: cursor first, then
#     surface id;
#   - provenance (known) points: exact data points a beam passes through --
#     the player (initial beam, t = 0), the cursor (whenever it is a
#     candidate: plan complete and matched, the beam is B_K), the departure
#     point of a beam born at an exact hit point, every known point of the
#     previous beam on the carrier of the surface applied (an effect fixes
#     its carrier pointwise), and every segment endpoint on an exact line.
#     Pops restore the earlier beam's known points;
#   - exact lines: a straight beam defined by exact data, kept in implicit
#     form a x + b y = c with exact coefficients -- the initial beam of a
#     direction trace or of a trace without plan (player -> cursor), and the
#     reflection of an exact line in a line mirror (an exact polynomial map of
#     the coefficients, whatever the hit point's rounding);
#   - incidence (Exact predicates on the data): a surface whose carrier
#     contains a known point exactly is hit exactly there (its nearest root
#     takes the point's parameter and the exact point). No rounding decides
#     an endpoint or an incidence;
#
# One walker implements the rules for every tier. A backend supplies the beam
# math: NumBackend (HPBeam over any NumCtx) or FastSolver.Backend (raw float64
# with the tangent-linear derivative inline, bit-identical to NumBackend over
# F64Num). Topology decisions (on-segment, side, endpoint blockage) always run
# the shared precision-generic BeamGeom code.
#
# Anchored planned path: beam B_j (after plan entry j) is the circle through
# P_j = F_j(player), X_j = F_j(F_K^-1(cursor)) and Q_j = F_j(inf), oriented
# P -> X -> Q, with P - Q tracked exactly (AnchorChain). Each B_j is built
# independently, so errors never accumulate along the plan. The walker swaps
# B_{j+1} in whenever plan entry j fires on the anchored path (PLANNED mode,
# or PHYSICAL mode before its first divergence from the planned decisions).
# After the divergence (or the cursor) the trace is a forward continuation,
# seeded for ErrorEstimate and solved on PrecisionLadder.

enum Mode { PHYSICAL = 0, PLANNED = 1 }

const MAX_HITS := 32
# Anchored-prefix gains are scaled to their own calibration constant
# (C_prefix = ErrorEstimate.C * PREFIX_GAIN_SCALE): their errors do not
# compound like the forward continuation's (calibrated on the bench corpus).
const PREFIX_GAIN_SCALE := 64.0 / 4096.0
const TRANSFORMATIVE := ["reflection", "inversion", "portal"]
const PROJECTIVE := ["line_normal_projection", "circle_normal_projection", "line_directional_projection",
	"circle_directional_projection"]

class Event extends RefCounted:
	var kind: String          # "hit" | "cursor" | "origin" | "escape" | "max"
	var surface_id: int = -1
	var left: bool = false    # side the beam comes from: true = LEFT
	var applied: bool = false # the side's effect fired (terminal included)
	var terminal: bool = false
	var on_segment: bool = false
	var rehit: bool = false   # the departure point, re-hit after the full loop
	var popped: bool = false  # the applied effect popped the transform stack
	var teleport: bool = false # the applied effect was a portal (the next arc starts elsewhere)
	var projective: bool = false # the applied effect was a projective
	var epoch: int = 0        # projectives start a new epoch (the stack is cleared)
	var point: CN = null      # null for escape
	var beam: HPBeam          # beam the arc leading to this event lies on
	var t_from                # arc start parameter on `beam`
	var t_to                  # arc end parameter on `beam` (null = infinity)
	var margin: float = INF   # px gap to the nearest competing candidate or
	                          # endpoint of the decision that produced this event
	var seeded: bool = false  # lies on the sensitive forward continuation
	var raw = null            # FastSolver: raw float beam of `beam` (provenance)
	var state: bool = false   # the side's state change fired
	var flags: Dictionary = {} # the trace's flags after this event (hits)

	func describe() -> String:
		var p := "" if point == null else " (%.6f, %.6f)" % [point.fx(), point.fy()]
		return "%s s=%d %s%s%s%s%s" % [kind, surface_id, "L" if left else "R", " applied" if applied else "",
			"" if on_segment else " off", p, (" state->%s" % str(flags)) if state else ""]

class PlanItem extends RefCounted:
	var surface_id: int
	var left: bool
	var hint = null           # Vector2: where the player clicked (settles a fork), or null
	static func make(sid: int, p_left: bool, p_hint = null) -> PlanItem:
		var o := PlanItem.new()
		o.surface_id = sid
		o.left = p_left
		o.hint = p_hint
		return o

# Transform-stack entry: the beam before applying surface `sid` at t_hit, and
# the other intersection of that beam with sid (where a cancelling re-hit of
# sid resumes it).
class StackEntry extends RefCounted:
	var beam
	var sid: int
	var t_hit
	var other
	var known: Array = []     # the beam's known points (Walk.known)
	var line = null           # the beam's exact line data (Walk.line)
	var pop_id: int = -1      # the surface whose hit pops this entry (sid; a portal's pair)
	var etype: String = ""    # the map pushed (a pop needs the same map: an involution undone)
	static func make(p_beam, p_sid: int, p_t_hit, p_other, p_known: Array = [], p_line = null, p_pop_id: int = -1, p_etype: String = "") -> StackEntry:
		var o := StackEntry.new()
		o.beam = p_beam
		o.sid = p_sid
		o.t_hit = p_t_hit
		o.other = p_other
		o.known = p_known
		o.line = p_line
		o.pop_id = p_sid if p_pop_id < 0 else p_pop_id
		o.etype = p_etype
		return o

# ---------------------------------------------------------------- backend

# HPBeam over a NumCtx (any tier).
class NumBackend extends RefCounted:
	var ctx: NumCtx

	static func make(p_ctx: NumCtx) -> NumBackend:
		var b := NumBackend.new()
		b.ctx = p_ctx
		return b

	func roots(beam: HPBeam, surf: BeamScene.Surf, depart_on: bool) -> Array:
		return BeamGeom.hits(beam, surf.seg, depart_on)
	func point(beam: HPBeam, t) -> CN:
		return beam.at(t)
	func tangent(beam: HPBeam, t) -> CN:
		return beam.tangent_at(t)
	func origin(beam: HPBeam) -> CN:
		return beam.p
	func param_of(beam: HPBeam, z: CN):
		return beam.param_of(z)
	func apply(beam: HPBeam, t, surf: BeamScene.Surf, etype: String, h: CN = null) -> HPBeam:
		return BeamEffects.apply(beam, t, surf, etype, h)
	func project(beam: HPBeam, t, surf: BeamScene.Surf, left: bool, h: CN = null) -> HPBeam:
		return BeamEffects.project(beam, t, surf, left, h)
	func straight(beam: HPBeam) -> bool:
		return beam.kappa.is_zero()
	# the same beam with curvature exactly zero (provenance: it is a line)
	func straightened(beam: HPBeam) -> HPBeam:
		if beam.kappa is DualNum:
			return HPBeam.make(beam.p, beam.tau, DualNum.of(0.0, beam.kappa.d))
		return HPBeam.make(beam.p, beam.tau, beam.kappa.sub(beam.kappa))
	func zero():
		return ctx.zero()
	func seed(beam: HPBeam) -> HPBeam:
		return BeamSolver._seed(beam)
	func reseed(beam: HPBeam) -> HPBeam:
		return BeamSolver._reseed_if_unseeded(beam)
	func from_hp(b: HPBeam) -> HPBeam:
		return b
	func hp(beam: HPBeam) -> HPBeam:
		return beam
	func raw(_beam: HPBeam):
		return null
	func ev_t(t):
		return t
	func gain(beam: HPBeam, t) -> float:
		if not (beam.kappa is DualNum):
			return 0.0
		var h := beam.at(t)
		return sqrt(h.re.d * h.re.d + h.im.d * h.im.d)
	# [point, on_segment, side LEFT, fully blocked] of the hit at t on surf
	# (at_origin: the hit is the beam's own point p; known_pt: the exact point,
	# both by provenance).
	func classify(scene: BeamScene, beam: HPBeam, t, surf: BeamScene.Surf, at_origin: bool, known_pt = null) -> Array:
		var pt: CN = known_pt if known_pt != null else (beam.p if at_origin else beam.at(t))
		var tang := beam.tangent_at(t)
		var on_seg := BeamGeom.on_segment(surf.seg, pt)
		return [pt, on_seg, BeamGeom.side_left(surf.seg, pt, tang), BeamSolver._full_block(scene, surf, pt, tang, on_seg)]
	func prefix_gain(_beam: HPBeam, _t) -> float:
		return 0.0
	# float64 view of a point (decision margins only)
	func f64_point(beam: HPBeam, t) -> PackedFloat64Array:
		if beam.kappa is BigFloat:
			var bf := FastSolver.beam(beam.p.fx(), beam.p.fy(), beam.tau.fx(), beam.tau.fy(), beam.kappa.to_float())
			var h := FastSolver.at_view(bf, t.to_float())
			return PackedFloat64Array([h[0], h[1]])
		var z := beam.at(t)
		return PackedFloat64Array([z.fx(), z.fy()])

# ---------------------------------------------------------------- anchored chain

# Anchored beams B_0..B_K of a plan (one object per transform-stack frame: a
# popped frame IS the earlier beam object). null if the construction is
# degenerate (a pole or image at infinity, or player = cursor image).
class AnchorChain extends RefCounted:
	var beams: Array = []
	var hits := {}               # projective plan entry index -> its back-propagated hit point H
	var exit_lines := {}         # projective plan entry index -> exact exit line (Exact.Line), when exact data

	static func _key(st: Array) -> String:
		return ",".join(st.map(func(x): return str(x)))

	# The surface whose application pops an entry pushed by sid.
	static func _pop_id(scene: BeamScene, sid: int) -> int:
		var s: BeamScene.Surf = scene.by_id.get(sid)
		return s.pair if s != null and s.portal else sid

	# Projective entries split the plan into sub-chains (GAME_SPEC 13.3/13.4):
	# backward, each projective's hit point H is back-propagated from the
	# target behind it (the cursor pulled back through the entries after it);
	# each sub-chain is anchored from its origin (the player, or the previous
	# H) to its target (the next H, or the cursor). Beams are flat: beams[j] is
	# the beam after j consumed entries.
	static func build(scene: BeamScene, player: CN, cursor: CN, plan: Array, flags: Dictionary = {}) -> AnchorChain:
		var effs := BeamSolver.plan_effects(scene, plan, flags)
		var proj: Array = []
		for j in plan.size():
			if effs[j] in PROJECTIVE:
				proj.append(j)
		if proj.is_empty():
			return _build_simple(scene, player, cursor, plan, effs)
		# backward: the targets of the sub-chains (a fork settled by the click)
		var H = backward_hits(scene, plan, effs, cursor)
		if H == null:
			return null
		for j in proj:
			if not H.has(j):
				return null    # an entry without a hit point: the greedy aim
		return from_hits(scene, player, cursor, plan, flags, effs, H)

	# The backward pass (oracle trace2._backward): {projective entry index: H},
	# or null when an image is at infinity. A circle projective whose target
	# lies inside the circle has two candidate hit points; the fork is settled
	# at planning time: the candidate nearer the point the player clicked
	# (PlanItem.hint), by default the segment's midpoint (BeamEffects.
	# back_propagate). An entry without a candidate is left out (skipped by
	# the aim).
	static func backward_hits(scene: BeamScene, plan: Array, effs: Array, cursor: CN):
		var hits := {}
		var X = cursor
		var on_sid := -2         # the surface whose carrier X lies on (provenance)
		for j in range(plan.size() - 1, -1, -1):
			var surf: BeamScene.Surf = scene.by_id.get(plan[j].surface_id)
			if surf == null:
				continue
			if effs[j] in PROJECTIVE:
				var on: bool = on_sid == surf.id or (on_sid == -2 and BeamSolver._on_carrier(surf, X))
				var Hc = BeamEffects.back_propagate(surf, plan[j].left, X, on, plan[j].hint)
				if Hc == null:
					continue
				X = Hc
				hits[j] = Hc
				on_sid = surf.id
			elif effs[j] in TRANSFORMATIVE:
				on_sid = -1
				X = BeamEffects.map_point(X, surf, true)
				if X == null:
					return null
		return hits

	# The chain of sub-chains through the given projective hit points.
	static func from_hits(scene: BeamScene, player: CN, cursor: CN, plan: Array, flags: Dictionary, effs: Array,
			H: Dictionary) -> AnchorChain:
		var proj: Array = []
		for j in plan.size():
			if effs[j] in PROJECTIVE:
				proj.append(j)
		var chain := AnchorChain.new()
		var a := 0
		var origin: CN = player
		var exit_dir = null      # the exit at origin of the previous projective
		for b in proj + [plan.size()]:
			var target: CN = H[b] if b < plan.size() else cursor
			var sub := _build_simple(scene, origin, target, plan.slice(a, b), effs.slice(a, b))
			if sub == null:
				return null
			var beams: Array = sub.beams
			if exit_dir != null:
				# after a projective the beam leaves along its exit; when that
				# points away from the target, the target is reached through
				# infinity: the sub-chain's beams run the other way round
				var b0: HPBeam = beams[0]
				var rev: bool = CN.dot(exit_dir, b0.tau).is_negative()
				# the exit beam itself is exactly straight (a projective exit is a
				# line by definition, not a circle through rounded points); a
				# frame popped back to it is the same object
				var straight := HPBeam.make(origin, exit_dir.unit(), b0.kappa.sub(b0.kappa))
				beams = beams.map(func(bm):
					if is_same(bm, b0):
						return straight
					return HPBeam.make(bm.p, bm.tau.neg(), bm.kappa.neg()) if rev else bm)
			chain.beams.append_array(beams)
			if b < plan.size():
				chain.hits[b] = H[b]
				if b == proj.back():
					var L = BeamSolver._exit_line(scene, cursor, plan, flags, b)
					if L != null:
						chain.exit_lines[b] = L
				origin = H[b]
				exit_dir = BeamEffects.projective_exit(scene.by_id[plan[b].surface_id], plan[b].left, H[b])
			a = b + 1
		return chain

	static func _build_simple(scene: BeamScene, player: CN, cursor: CN, plan: Array, effs: Array) -> AnchorChain:
		var K := plan.size()
		var stacks: Array = [[]]
		var st: Array = []
		var changes: Array = []      # plan entry j changes the frame
		for ji in plan.size():
			var item = plan[ji]
			var surf: BeamScene.Surf = scene.by_id.get(item.surface_id)
			var t: bool = surf != null and effs[ji] in TRANSFORMATIVE
			changes.append(t)
			if t:
				if not st.is_empty() and _pop_id(scene, st.back()) == item.surface_id:
					st.pop_back()
				else:
					st.append(item.surface_id)
			stacks.append(st.duplicate())
		# pole chain: key -> [P, Q or null, delta or null]
		var memo := {"": [player, null, null]}
		for s in stacks:
			for n in range(1, s.size() + 1):
				var key := _key(s.slice(0, n))
				if memo.has(key):
					continue
				var parent: Array = memo[_key(s.slice(0, n - 1))]
				if parent[0] == null:
					return null
				memo[key] = BeamEffects.map_pole(parent, scene.by_id[s[n - 1]])
		# image chain X_j, memoized by frame (exact provenance across pops)
		var X: Array = []
		X.resize(K + 1)
		var seen := {}
		for j in range(K, -1, -1):
			var key := _key(stacks[j])
			if seen.has(key):
				X[j] = seen[key]
			elif j == K:
				X[j] = cursor
			elif not changes[j]:
				X[j] = X[j + 1]
			else:
				if X[j + 1] == null:
					return null
				X[j] = BeamEffects.map_point(X[j + 1], scene.by_id[plan[j].surface_id], true)
			seen[key] = X[j]
		var chain := AnchorChain.new()
		var beam_of := {}
		for j in K + 1:
			var key := _key(stacks[j])
			if not beam_of.has(key):
				var pc: Array = memo[key]
				if pc[0] == null or X[j] == null or X[j].sub(pc[0]).is_zero():
					return null
				beam_of[key] = HPBeam.through(pc[0], X[j], pc[2])
			chain.beams.append(beam_of[key])
		return chain

# ---------------------------------------------------------------- walk state

class Walk extends RefCounted:
	var bk
	var scene: BeamScene
	var plan: Array = []
	var cursor = null         # CN or null
	var mode: int = Mode.PHYSICAL
	var post_mode: int = Mode.PHYSICAL
	var plan_index: int = 0
	var plan_matched: bool = true
	var cursor_done: bool = false
	var count: int = 0
	var max_events: int = MAX_HITS
	var events: Array = []
	var gains: Array = []
	var stack: Array = []
	var done: bool = false
	var seeded: bool = false
	var chain: AnchorChain = null
	var anchored: bool = false
	var epoch: int = 0
	var known: Array = []     # known points on the current beam: [CN, t, skip surface id]
	var line = null           # exact implicit line of the current beam (Exact.Line), or null
	var incidence := {}       # cache: [x, y, surface id] -> exact incidence
	var tangent_line = null   # the line `tangent` was computed for
	var tangent := {}         # surface id -> exact tangency of tangent_line
	var flags: Dictionary = {} # trace state flags (GAME_SPEC 9.4 / 10.6)
	var on_plan: bool = true   # on the planned beams: every projective fired at its H (oracle Walk.on_plan);
	                           # a transformative entry maps the whole plane: any crossing gives the planned beam
	var hp_src = null         # beam whose HPBeam view is hp_view (events share it)
	var hp_view: HPBeam = null

# ---------------------------------------------------------------- entry points

# Walk an explicit direction from the player (forward shooting).
static func trace_direction(ctx: NumCtx, scene: BeamScene, player: CN, dir: CN, plan: Array, cursor, mode: int,
		max_events: int = MAX_HITS, flags: Dictionary = {}) -> Array:
	return run_direction(NumBackend.make(ctx), scene, player, dir, plan, cursor, mode, max_events, flags).events

static func run_direction(bk, scene: BeamScene, player: CN, dir: CN, plan: Array, cursor, mode: int,
		max_events: int = MAX_HITS, flags: Dictionary = {}) -> Walk:
	var w := _new_walk(bk, scene, plan, cursor, mode, max_events, flags)
	var b0 = bk.from_hp(HPBeam.make(player, dir.unit(), player.re.sub(player.re)))
	if mode == Mode.PHYSICAL:
		b0 = bk.seed(b0)
		w.seeded = true
	w.known = [[player, bk.zero(), -1]]
	w.line = Exact.Line.from_exact(Exact.line_from_dir(player.fx(), player.fy(), dir.fx(), dir.fy()))
	_add_line_endpoints(w, b0)
	walk_from(w, b0, -1, null)
	return w

# Segment data points (endpoints, via) exactly on the current exact line are
# known points.
static func _add_line_endpoints(w: Walk, b) -> void:
	for s in w.scene.surfaces:
		if s.seg.full:
			continue
		for ep in [s.seg.s, s.seg.e, s.seg.v]:
			if ep == null:
				continue
			if _known_index(w.known, ep) < 0 and w.line.contains(ep.fx(), ep.fy()):
				w.known.append([ep, w.bk.param_of(b, ep), -1])

# Exact: the known point k lies on surf's carrier (cached per walk).
static func _incident(w: Walk, k: CN, surf: BeamScene.Surf) -> bool:
	if BeamGeom.endpoint_at(surf.seg, k) != 0:
		return true
	var kx := k.fx()
	var ky := k.fy()
	var key := [kx, ky, surf.id]
	if w.incidence.has(key):
		return w.incidence[key]
	var r: bool
	if surf.line:
		r = Exact.orient_zero(surf.sx, surf.sy, surf.ex, surf.ey, kx, ky)
	elif surf.full:
		r = Exact.on_circle_zero(surf.cx, surf.cy, surf.r, kx, ky)
	else:
		r = Exact.incircle_zero(surf.sx, surf.sy, surf.vx, surf.vy, surf.ex, surf.ey, kx, ky)
	w.incidence[key] = r
	return r

# Planned trace (anchored), then its post-cursor physical continuation.
static func planned(ctx: NumCtx, scene: BeamScene, player: CN, cursor: CN, plan: Array, max_events: int = MAX_HITS, flags: Dictionary = {}) -> Array:
	var w := run(NumBackend.make(ctx), scene, player, cursor, plan, Mode.PLANNED, max_events, flags)
	return [] if w == null else w.events

# Physical trace: on the anchored planned path until the first divergence.
static func physical(ctx: NumCtx, scene: BeamScene, player: CN, cursor: CN, plan: Array, max_events: int = MAX_HITS, flags: Dictionary = {}) -> Array:
	var w := run(NumBackend.make(ctx), scene, player, cursor, plan, Mode.PHYSICAL, max_events, flags)
	return [] if w == null else w.events

# null if the anchored construction is degenerate (player = cursor, or a pole
# / image at infinity: Stage 83).
static func run(bk, scene: BeamScene, player: CN, cursor: CN, plan: Array, mode: int, max_events: int = MAX_HITS,
		flags: Dictionary = {}) -> Walk:
	var chain := _select_chain(bk, scene, player, cursor, plan, max_events, flags)
	if chain == null:
		return _run_degenerate(bk, scene, player, cursor, plan, mode, max_events, flags)
	return _walk_chain(bk, scene, player, cursor, plan, mode, max_events, flags, chain)

# The anchored chain of a plan (forks settled by the clicks: no search).
static func _select_chain(_bk, scene: BeamScene, player: CN, cursor: CN, plan: Array, _max_events: int,
		flags: Dictionary) -> AnchorChain:
	return AnchorChain.build(scene, player, cursor, plan, flags)

static func _walk_chain(bk, scene: BeamScene, player: CN, cursor: CN, plan: Array, mode: int, max_events: int,
		flags: Dictionary, chain: AnchorChain) -> Walk:
	var w := _new_walk(bk, scene, plan, cursor, mode, max_events, flags)
	w.chain = chain
	w.anchored = true
	var b0 = bk.from_hp(chain.beams[0])
	if mode == Mode.PHYSICAL and plan.is_empty():
		# no plan: the physical trace is the forward continuation from the start
		b0 = bk.seed(b0)
		w.seeded = true
		w.anchored = false
	w.known = [[player, bk.zero(), -1]]
	w.line = _initial_line(scene, player, cursor, plan, flags)
	if w.line != null:
		# every frame change fixes the cursor: the aim image is the cursor
		# itself, exact data B_0 passes through
		if not plan.is_empty() and fixes_cursor(scene, cursor, plan, flags) and _known_index(w.known, cursor) < 0:
			w.known.append([cursor, bk.param_of(b0, cursor), -1])
		_add_line_endpoints(w, b0)
	walk_from(w, b0, -1, null)
	return w

# Exact: the point k (float data, e.g. the cursor) lies on surf's carrier.
static func _on_carrier(surf: BeamScene.Surf, k: CN) -> bool:
	if BeamGeom.endpoint_at(surf.seg, k) != 0:
		return true
	if surf.line:
		return Exact.orient_zero(surf.sx, surf.sy, surf.ex, surf.ey, k.fx(), k.fy())
	if surf.full:
		return Exact.on_circle_zero(surf.cx, surf.cy, surf.r, k.fx(), k.fy())
	return Exact.incircle_zero(surf.sx, surf.sy, surf.vx, surf.vy, surf.ex, surf.ey, k.fx(), k.fy())

# Exact: every frame change of the plan fixes the cursor (it lies on the
# carrier of each planned reflection / inversion -- an effect fixes its
# carrier pointwise; a translation portal fixes nothing).
static func fixes_cursor(scene: BeamScene, cursor: CN, plan: Array, flags: Dictionary = {}) -> bool:
	var kx := cursor.fx()
	var ky := cursor.fy()
	var effs := plan_effects(scene, plan, flags)
	if effs.any(func(e): return e in PROJECTIVE):
		return false
	for ji in plan.size():
		var item = plan[ji]
		var surf: BeamScene.Surf = scene.by_id.get(item.surface_id)
		if surf == null or not (effs[ji] in TRANSFORMATIVE):
			continue
		if surf.portal:
			return false
		var on: bool
		if BeamGeom.endpoint_at(surf.seg, cursor) != 0:
			on = true
		elif surf.line:
			on = Exact.orient_zero(surf.sx, surf.sy, surf.ex, surf.ey, kx, ky)
		elif surf.full:
			on = Exact.on_circle_zero(surf.cx, surf.cy, surf.r, kx, ky)
		else:
			on = Exact.incircle_zero(surf.sx, surf.sy, surf.vx, surf.vy, surf.ex, surf.ey, kx, ky)
		if not on:
			return false
	return true

# The exact implicit line of B_0, when it is exact data: the line player ->
# the exact aim image. Lazy: float64 coefficients with rigorous bounds from
# the image chain's running error analysis; the exact homogeneous image (whose
# size grows with every inversion) only when a predicate needs it.
static func _initial_line(scene: BeamScene, player: CN, cursor: CN, plan: Array, flags: Dictionary = {}):
	var X = exact_image_approx(scene, cursor, plan, flags)
	if X == null:
		return null
	var px := player.fx()
	var py := player.fy()
	# a = -(y - py w), b = x - px w, c = a px + b py
	var a := Exact._esub(Exact._emul([py, 0.0], X[2]), X[1])
	var b := Exact._esub(X[0], Exact._emul([px, 0.0], X[2]))
	var c := Exact._eadd(Exact._emul(a, [px, 0.0]), Exact._emul(b, [py, 0.0]))
	return Exact.Line.from_approx(a, b, c, func():
		var Xh = _exact_image_cached(scene, cursor, plan, flags)
		return null if Xh == null else Exact.line_through_h(px, py, Xh))

# Budget of the exact aim image (limbs of its homogeneous weight). The exact
# image roughly doubles in size per inversion; 320 limbs is about four
# inversions, ~5 ms (the frame budget). Beyond it the initial line makes no
# exact claim -- its decisions go to the precision ladder, as they did before
# inversion plans had exact lines; a fixed-point cursor is exact at any depth.
const EXACT_IMAGE_LIMBS := 320
static var _image_cache := {}

# The exact aim image within the budget (null beyond it), computed once per
# (cursor, plan, plan surfaces' data).
static func _exact_image_cached(scene: BeamScene, cursor: CN, plan: Array, flags: Dictionary = {}):
	var key := [cursor.fx(), cursor.fy()]
	var effs := plan_effects(scene, plan, flags)
	for ji in plan.size():
		var item = plan[ji]
		var s: BeamScene.Surf = scene.by_id.get(item.surface_id)
		key.append_array([item.surface_id, item.left] if s == null else [item.surface_id, item.left, effs[ji],
			s.sx, s.sy, s.ex, s.ey, s.vx if s.has_v else 0.0, s.vy if s.has_v else 0.0, s.cx, s.cy, s.r, s.dx, s.dy, s.theta])
	if _image_cache.has(key):
		return _image_cache[key]
	if _image_cache.size() > 256:
		_image_cache.clear()
	var X = exact_image_h(scene, cursor, plan, EXACT_IMAGE_LIMBS, flags)
	_image_cache[key] = X
	return X

# The aim image as float64 homogeneous coordinates with rigorous bounds
# ([[x, ex], [y, ey], [w, ew]]), along the same maps as exact_image_h; each
# step rescales by a power of two (exact) so magnitudes stay in range.
static func exact_image_approx(scene: BeamScene, cursor: CN, plan: Array, flags: Dictionary = {}):
	var X: Array = [[cursor.fx(), 0.0], [cursor.fy(), 0.0], [1.0, 0.0]]
	var effs := plan_effects(scene, plan, flags)
	if effs.any(func(e): return e in PROJECTIVE):
		return null      # a back-propagated hit point is not exact data
	for j in range(plan.size() - 1, -1, -1):
		var surf: BeamScene.Surf = scene.by_id.get(plan[j].surface_id)
		if surf == null or not (effs[j] in TRANSFORMATIVE):
			continue
		if surf.portal:
			if surf.theta != 0.0:
				return null
			X = [Exact._esub(X[0], Exact._emul([surf.dx, 0.0], X[2])), Exact._esub(X[1], Exact._emul([surf.dy, 0.0], X[2])), X[2]]
			continue
		if surf.line:
			X = _reflect_approx(X, surf.sx, surf.sy, surf.ex, surf.ey)
		else:
			var ec: Array = surf.exact_circle()
			var C := [_approx(ec[0][0]), _approx(ec[0][1]), _approx(ec[0][2])]
			var rw2 := _approx(ec[1])
			var w: Array = X[2]
			var cw: Array = C[2]
			var dx := Exact._esub(Exact._emul(X[0], cw), Exact._emul(C[0], w))
			var dy := Exact._esub(Exact._emul(X[1], cw), Exact._emul(C[1], w))
			var dd := Exact._eadd(Exact._emul(dx, dx), Exact._emul(dy, dy))
			var rw := Exact._emul(rw2, w)
			X = [Exact._eadd(Exact._emul(C[0], dd), Exact._emul(rw, dx)),
				Exact._eadd(Exact._emul(C[1], dd), Exact._emul(rw, dy)), Exact._emul(cw, dd)]
		# rescale by a power of two near 1 / |w| (exact on values and bounds)
		var m := absf(X[2][0])
		if m == 0.0 or not is_finite(m):
			return null
		var k := pow(2.0, -floorf(log(m) / log(2.0)))
		X = [[X[0][0] * k, X[0][1] * k], [X[1][0] * k, X[1][1] * k], [X[2][0] * k, X[2][1] * k]]
	return X

# A BigFloat as a [float64 value, bound] pair (to_float rounds to nearest).
static func _approx(v: BigFloat) -> Array:
	var f := v.to_float()
	return [f, 2.0 * Exact.U * absf(f)]

# reflect_point_h on [value, bound] pairs.
static func _reflect_approx(X: Array, sx: float, sy: float, ex: float, ey: float) -> Array:
	var mx := Exact._esub([ex, 0.0], [sx, 0.0])
	var my := Exact._esub([ey, 0.0], [sy, 0.0])
	var mm := Exact._eadd(Exact._emul(mx, mx), Exact._emul(my, my))
	var m2r := Exact._esub(Exact._emul(mx, mx), Exact._emul(my, my))
	var m2i := Exact._eadd(Exact._emul(mx, my), Exact._emul(my, mx))
	var w: Array = X[2]
	var cx := Exact._esub(X[0], Exact._emul([sx, 0.0], w))
	var cy := Exact._esub(Exact._emul([sy, 0.0], w), X[1])
	var swm := Exact._emul(w, mm)
	var rx := Exact._eadd(Exact._emul([sx, 0.0], swm), Exact._esub(Exact._emul(m2r, cx), Exact._emul(m2i, cy)))
	var ry := Exact._eadd(Exact._emul([sy, 0.0], swm), Exact._eadd(Exact._emul(m2r, cy), Exact._emul(m2i, cx)))
	return [rx, ry, swm]

# The aim image F_K^-1(cursor) as an exact homogeneous point [x, y, w], when
# every frame change of the plan is a rational map of exact data (a line
# reflection, an inversion in an exact circle, a translation portal); null for
# a rotation portal or an image at infinity.
static func exact_image_h(scene: BeamScene, cursor: CN, plan: Array, max_limbs: int = 0, flags: Dictionary = {},
		from_index: int = 0):
	var X: Array = [Exact.of(cursor.fx()), Exact.of(cursor.fy()), Exact.of(1.0)]
	var effs := plan_effects(scene, plan, flags)
	if effs.slice(from_index).any(func(e): return e in PROJECTIVE):
		return null
	for j in range(plan.size() - 1, from_index - 1, -1):
		var surf: BeamScene.Surf = scene.by_id.get(plan[j].surface_id)
		if surf == null or not (effs[j] in TRANSFORMATIVE):
			continue
		if surf.portal:
			if surf.theta != 0.0:
				return null
			# T^-1(X) = X - d (homogeneous: X - d w)
			X = [Exact.sub(X[0], Exact.mul(Exact.of(surf.dx), X[2])), Exact.sub(X[1], Exact.mul(Exact.of(surf.dy), X[2])), X[2]]
			continue
		if not surf.line:
			# inversion in the exact circle: a rational map of exact data
			var ec: Array = surf.exact_circle()
			X = Exact.invert_point_h(X, ec[0], ec[1])
			if X == null:
				return null
			if max_limbs > 0 and X[2].precision_limbs() > max_limbs:
				return null
			continue
		X = Exact.reflect_point_h(X, surf.sx, surf.sy, surf.ex, surf.ey)
	return X

# A point at infinity in the anchored construction (player / cursor exactly at
# an inversion center): forward walk from the exact aim direction, or -- when
# the aim image itself is at infinity (the aim is undetermined) -- straight at
# the cursor (oracle trace2._aim). null only for player = cursor.
static func _run_degenerate(bk, scene: BeamScene, player: CN, cursor: CN, plan: Array, mode: int, max_events: int,
		flags: Dictionary = {}) -> Walk:
	var effs := plan_effects(scene, plan, flags)
	var X = cursor
	var on_sid := -2
	for j in range(plan.size() - 1, -1, -1):
		var surf: BeamScene.Surf = scene.by_id.get(plan[j].surface_id)
		if surf != null and effs[j] in PROJECTIVE and X != null:
			# an entry without a back-propagated point is skipped (oracle aim)
			var on: bool = on_sid == surf.id or (on_sid == -2 and _on_carrier(surf, X))
			var H = BeamEffects.back_propagate(surf, plan[j].left, X, on, plan[j].hint)
			if H != null:
				X = H
				on_sid = surf.id
			continue
		if surf != null and effs[j] in TRANSFORMATIVE:
			on_sid = -1
		if surf == null or not (effs[j] in TRANSFORMATIVE):
			continue
		X = BeamEffects.map_point(X, surf, true)
		if X == null:
			break
	var dir: CN
	var line = null
	if X == null or X.sub(player).is_zero():
		dir = cursor.sub(player)
		if dir.is_zero():
			return null
		line = Exact.Line.from_exact(Exact.line_through(player.fx(), player.fy(), cursor.fx(), cursor.fy()))
	else:
		dir = X.sub(player)
		line = _initial_line(scene, player, cursor, plan, flags)
	var w := _new_walk(bk, scene, plan, cursor, mode, max_events, flags)
	var b0 = bk.from_hp(HPBeam.make(player, dir.unit(), player.re.sub(player.re)))
	if mode == Mode.PHYSICAL:
		b0 = bk.seed(b0)
		w.seeded = true
	w.known = [[player, bk.zero(), -1]]
	w.line = line
	if w.line != null:
		if X != null and not X.sub(player).is_zero() and not plan.is_empty() and fixes_cursor(scene, cursor, plan, flags) \
				and _known_index(w.known, cursor) < 0:
			w.known.append([cursor, bk.param_of(b0, cursor), -1])
		_add_line_endpoints(w, b0)
	walk_from(w, b0, -1, null)
	return w

static func _new_walk(bk, scene: BeamScene, plan: Array, cursor, mode: int, max_events: int, flags: Dictionary = {}) -> Walk:
	var w := Walk.new()
	w.flags = flags
	w.bk = bk
	w.scene = scene
	w.plan = plan
	w.cursor = cursor
	w.mode = mode
	w.post_mode = Mode.PHYSICAL
	w.max_events = max_events
	return w

# Forward state simulation (GAME_SPEC 13.7): the effect each plan entry
# resolves to, under the flags left by the entries before it (each entry
# applies its state change when consumed).
static func plan_effects(scene: BeamScene, plan: Array, flags: Dictionary = {}) -> Array:
	var f := flags
	var out: Array = []
	for item in plan:
		var surf: BeamScene.Surf = scene.by_id.get(item.surface_id)
		if surf == null:
			out.append("")
			continue
		out.append(surf.effect(item.left, f))
		var ch = surf.state_change(item.left, f)
		if ch != null:
			f = apply_state(f, ch)
	return out

# The flags after a state change {key, op: "set" | "toggle", value} (a new
# dictionary): toggle stores the negation of the current truth value.
static func apply_state(flags: Dictionary, change: Dictionary) -> Dictionary:
	var out := flags.duplicate()
	if String(change["op"]) == "toggle":
		out[change["key"]] = not bool(flags.get(change["key"], false))
	else:
		out[change["key"]] = change["value"]
	return out

# The map an effect applies: reflection on a circle carrier is the inversion
# in it.
static func map_kind(surf: BeamScene.Surf, etype: String) -> String:
	if etype == "reflection" and not surf.line:
		return "inversion"
	return etype

# Ids of target surfaces hit on-segment.
static func targets_of(scene: BeamScene, events: Array) -> Array:
	var ids := {}
	for e in events:
		if e.kind == "hit" and e.on_segment:
			var s: BeamScene.Surf = scene.by_id.get(e.surface_id)
			if s != null and s.is_target:
				ids[e.surface_id] = true
	var out: Array = ids.keys()
	out.sort()
	return out

# ---------------------------------------------------------------- walker

# Projective order along a beam starting at t0: true if a comes before b.
static func _before(a, b, t0) -> bool:
	var ka: int = 0 if a.cmp(t0) > 0 else 1
	var kb: int = 0 if b.cmp(t0) > 0 else 1
	if ka != kb:
		return ka < kb
	return a.cmp(b) < 0

# Candidates [t, surf (null = cursor), rehit, exact point or null] in walk order.
static func _candidates(w: Walk, beam, depart: int, fresh: bool, t0) -> Array:
	var bk = w.bk
	var cands: Array = []
	for s in w.scene.surfaces:
		if w.line != null and not s.line and _tangent(w, s):
			continue      # a tangent touch is not a crossing (exact)
		var rs: Array
		if fresh:
			rs = bk.roots(beam, s, depart == s.id)
			if depart == s.id:
				rs = rs.filter(func(t): return not t.is_zero())
		else:
			rs = bk.roots(beam, s, false)
			if depart == s.id:
				rs = rs.filter(func(t): return t.cmp(t0) != 0)
		for t in rs:
			cands.append([t, s, false, null])
	var cursor_now := false
	# the cursor is on the beam only on the planned path (its final anchored
	# beam passes through the cursor by construction); off it, a cursor is
	# never "reached" at its projection
	if not w.cursor_done and w.plan_index >= w.plan.size() and w.plan_matched and w.cursor != null and w.on_plan:
		var tc = bk.param_of(beam, w.cursor)
		var ki := _known_index(w.known, w.cursor)
		# the cursor exactly at the departure point is reached immediately
		if (fresh and BeamGeom._eq(bk.origin(beam), w.cursor)) \
				or (ki >= 0 and w.known[ki][2] == depart and depart >= 0 and w.known[ki][1].cmp(t0) == 0):
			tc = t0
			cursor_now = true
		cands.append([tc, null, false, null])
		if ki < 0:
			w.known.append([w.cursor, tc, -1])
	for kp in w.known:
		_snap(w, beam, cands, kp)
	cands.sort_custom(func(a, b):
		if cursor_now and (a[1] == null) != (b[1] == null):
			return a[1] == null
		var c: int = a[0].cmp(b[0])
		if c != 0:
			return _before(a[0], b[0], t0)
		var ia: int = -1 if a[1] == null else a[1].id
		var ib: int = -1 if b[1] == null else b[1].id
		return ia < ib)
	if w.line != null:
		_concurrent_ties(w, cands)
	if depart >= 0:
		# the departure re-hit is at the departure point (exact when known)
		var dep_pt = null
		for k in w.known:
			if k[2] == depart and k[1].cmp(t0) == 0:
				dep_pt = k[0]
		cands.append([t0, w.scene.by_id[depart], true, dep_pt])
	return cands

static func _known_index(known: Array, z: CN) -> int:
	for i in known.size():
		if BeamGeom._eq(known[i][0], z):
			return i
	return -1

# On an exact line, adjacent crossings of two line carriers that meet the line
# at the same point (exact concurrency) share one parameter, ordered by id; the
# pair is marked (cand[4]) so its zero gap is not a decision margin.
static func _concurrent_ties(w: Walk, cands: Array) -> void:
	for i in cands.size() - 1:
		var x: Array = cands[i]
		var y: Array = cands[i + 1]
		if x[1] == null or y[1] == null or x[2] or y[2] or not x[1].line or not y[1].line:
			continue
		var p: BeamScene.Surf = x[1]
		var q: BeamScene.Surf = y[1]
		if not w.line.concurrent(p.sx, p.sy, p.ex, p.ey, q.sx, q.sy, q.ex, q.ey):
			continue
		var lo: BeamScene.Surf = p if p.id < q.id else q
		var hi: BeamScene.Surf = q if p.id < q.id else p
		var t = x[0]
		var pt = x[3] if x[3] != null else y[3]
		cands[i] = [t, lo, false, pt, i]
		cands[i + 1] = [t, hi, false, pt, i]

# A surface whose carrier contains the known point kp (on the beam at kt) is
# hit exactly there: its root nearest kp becomes (kt, kp). skip: the
# departure surface for the departure point (its departure root is excluded).
static func _snap(w: Walk, beam, cands: Array, rec: Array) -> void:
	var kp: CN = rec[0]
	var kt = rec[1]
	var skip: int = rec[2]
	if rec.size() < 4:
		# the surfaces whose carrier contains kp (exact; once per record)
		var inc: Array = []
		for s0 in w.scene.surfaces:
			if _incident(w, kp, s0):
				inc.append(s0)
		rec.append(inc)
	for s in rec[3]:
		if s.id == skip:
			continue
		# which of the (distinct) roots is the known point: compared at the
		# tier's precision
		var best := -1
		var best_d := INF
		for i in cands.size():
			if cands[i][1] != s:
				continue
			var d: float = w.bk.point(beam, cands[i][0]).sub(kp).abs2().to_float()
			if d < best_d:
				best_d = d
				best = i
		if best >= 0:
			cands[best] = [kt, s, false, kp]

# Per candidate: px distance to its neighbours in the walk order (and, for a
# segment, to its endpoints): an ordering / on-segment decision is only as
# reliable as this gap is large vs. the error. Used by ErrorEstimate to decide
# precision, never to decide topology.
static func _gaps(w: Walk, beam, cands: Array) -> Array:
	var pts: Array = []
	for cand in cands:
		if cand[3] != null:
			pts.append(PackedFloat64Array([cand[3].fx(), cand[3].fy()]))
		else:
			pts.append(w.bk.f64_point(beam, cand[0]))
	var gaps: Array = []
	for i in cands.size():
		var g := INF
		var s = cands[i][1]
		if s != null:
			# neighbours: surface candidates whose order against this one is a
			# numerical decision (two exact provenance points are ordered by
			# their exact shared parameter and the id rule, not by rounding)
			var exact_i: bool = cands[i][3] != null
			var tie_i: int = cands[i][4] if cands[i].size() > 4 else -1
			var l := i - 1
			while l >= 0 and (cands[l][1] == null or (exact_i and cands[l][3] != null) \
					or (tie_i >= 0 and cands[l].size() > 4 and cands[l][4] == tie_i)):
				l -= 1
			if l >= 0:
				g = minf(g, _d64(pts[i], pts[l][0], pts[l][1]))
			var r := i + 1
			while r < cands.size() and (cands[r][1] == null or (exact_i and cands[r][3] != null) \
					or (tie_i >= 0 and cands[r].size() > 4 and cands[r][4] == tie_i)):
				r += 1
			if r < cands.size():
				g = minf(g, _d64(pts[i], pts[r][0], pts[r][1]))
			if not s.seg.full:
				var ds := _d64(pts[i], s.sx, s.sy)
				var de := _d64(pts[i], s.ex, s.ey)
				# an exact endpoint hit is decided by provenance, not by a margin
				if ds > 0.0:
					g = minf(g, ds)
				if de > 0.0:
					g = minf(g, de)
		gaps.append(g)
	return gaps

static func _d64(a: PackedFloat64Array, x: float, y: float) -> float:
	var dx := a[0] - x
	var dy := a[1] - y
	return sqrt(dx * dx + dy * dy)

# Blocked sides at an exact endpoint, incl. other segments sharing it.
static func _full_block(scene: BeamScene, surf: BeamScene.Surf, pt: CN, tang: CN, on_seg: bool) -> bool:
	if not on_seg:
		return false
	if BeamGeom.endpoint_at(surf.seg, pt) == 0:
		return true
	var bl := false
	var br := false
	for s in scene.surfaces:
		var which := BeamGeom.endpoint_at(s.seg, pt)
		if which == 0:
			continue
		var b := BeamGeom.blocked_sides(s.seg, which, tang)
		bl = bl or b[0]
		br = br or b[1]
	return bl and br

static func _plan_bookkeeping(w: Walk, sid: int) -> void:
	if w.plan_index < w.plan.size():
		if sid == w.plan[w.plan_index].surface_id:
			w.plan_index += 1
		else:
			w.plan_matched = false
	else:
		w.plan_matched = false
	if not w.plan_matched:
		w.on_plan = false         # a surface fired out of plan: off the planned beams

static func _event(w: Walk, kind: String, beam, t_from, t_to, margin: float) -> Event:
	var e := Event.new()
	e.kind = kind
	if w.hp_view == null or not is_same(w.hp_src, beam):
		w.hp_src = beam
		w.hp_view = w.bk.hp(beam)
	e.beam = w.hp_view
	e.raw = w.bk.raw(beam)
	e.t_from = w.bk.ev_t(t_from)
	e.t_to = null if t_to == null else w.bk.ev_t(t_to)
	if t_to != null:
		e.point = w.bk.point(beam, t_to)
	e.margin = margin
	e.seeded = w.seeded
	e.epoch = w.epoch
	w.events.append(e)
	var g := 0.0
	if t_to != null:
		if w.seeded:
			g = w.bk.gain(beam, t_to)
		elif w.anchored:
			# anchored prefix: sensitivity to the anchored beam's own angle
			# (its construction rounding), per beam -- grows with distance
			# from the anchor
			g = w.bk.prefix_gain(beam, t_to) * PREFIX_GAIN_SCALE
	w.gains.append(g)
	return e

# The other intersection of `beam` with surf, given the one applied at t.
# The other root is the one that is not the hit's crossing (at pt): the one
# farther from pt -- the same position rule that matches a known point to its
# root (the hit's parameter may be a snapped known point's, not bit-equal to
# the recomputed root).
static func _other_root(w: Walk, beam, surf: BeamScene.Surf, depart: int, t0, t, pt: CN):
	if surf.id == depart:
		return t0
	var rs: Array = w.bk.roots(beam, surf, false)
	if rs.size() < 2:
		return t   # tangent / straight beam: a single root
	var far = rs[0]
	var far_d := -1.0
	for r in rs:
		var d: float = w.bk.point(beam, r).sub(pt).abs2().to_float()
		if d > far_d:
			far_d = d
			far = r
	return far

# Forward walk. depart: id of the surface the beam departs from (-1: none).
# t_start null: fresh (the beam's own point p is the departure, t = 0);
# otherwise resumed at t_start.
static func walk_from(w: Walk, beam, depart: int, t_start) -> void:
	var bk = w.bk
	var fresh: bool = t_start == null
	var t0 = bk.zero() if fresh else t_start
	var skip := 0          # candidates already processed (in-place reseed)
	var t_prev = t0
	var fired_state := false   # a state change in the current loop (kept across in-place restarts)
	while not w.done:
		var cands := _candidates(w, beam, depart, fresh, t0)
		var gaps := _gaps(w, beam, cands)
		var next = null    # [beam, depart, t0, fresh] after an applied effect
		var restart := false
		var ci := skip
		skip = 0
		while ci < cands.size():
			var cand: Array = cands[ci]
			var t = cand[0]
			var surf: BeamScene.Surf = cand[1]
			var rehit: bool = cand[2]
			w.count += 1
			if w.count > w.max_events:
				# cut at the next crossing: drawn up to it, no effect
				var me := _event(w, "max", beam, t_prev, t, INF)
				if cand[3] != null:
					me.point = cand[3]
				elif rehit and fresh:
					me.point = bk.origin(beam)
				w.done = true
				return
			if surf == null:
				_event(w, "cursor", beam, t_prev, t, gaps[ci])
				t_prev = t
				w.cursor_done = true
				w.mode = w.post_mode
				# the post-cursor continuation is the sensitive part
				w.anchored = false
				if not w.seeded:
					w.seeded = true
					var sb = bk.seed(beam)
					if sb != beam:
						# same values, same candidates -- minus the cursor,
						# which is done: skip the ci processed before it
						beam = sb
						skip = ci
						restart = true
						break
				ci += 1
				continue
			var cl: Array = bk.classify(w.scene, beam, t, surf, rehit and fresh, cand[3])
			var pt: CN = cl[0]
			var on_seg: bool = cl[1]
			var left: bool = cl[2]
			var full_block: bool = cl[3]
			# the side under the trace's flags; its state change fires when the
			# side fires (PHYSICAL: a full block; PLANNED: the consumed entry)
			var eff := surf.effect(left, w.flags)
			var change = surf.state_change(left, w.flags)
			var plan_match: bool = w.plan_index < w.plan.size() and w.plan[w.plan_index].surface_id == surf.id \
				and w.plan[w.plan_index].left == left
			var plan_do: String = (eff if (eff in TRANSFORMATIVE or eff in PROJECTIVE) else "") if plan_match else ""
			var phys_do: String = eff if full_block else ""
			var plan_state: bool = plan_match and change != null
			var phys_state: bool = full_block and change != null
			var do := ""
			var fires_state := false
			var diverged := false
			if w.mode == Mode.PHYSICAL:
				do = phys_do
				fires_state = phys_state
				if w.anchored and not (phys_do == plan_do and phys_state == plan_state \
						and (phys_do != "" or phys_state or not plan_match)):
					diverged = true
				if do in TRANSFORMATIVE or do in PROJECTIVE or fires_state:
					_plan_bookkeeping(w, surf.id)
			else:
				if plan_match:
					w.plan_index += 1
				do = plan_do
				fires_state = plan_state
			if fires_state:
				w.flags = apply_state(w.flags, change)
				fired_state = true
			var e := _event(w, "hit", beam, t_prev, t, gaps[ci])
			e.surface_id = surf.id
			e.left = left
			e.state = fires_state
			e.flags = w.flags
			e.applied = do != ""
			e.terminal = do == "terminal"
			e.on_segment = on_seg
			e.rehit = rehit
			if (rehit and fresh) or cand[3] != null:
				e.point = pt
			t_prev = t
			if do == "terminal":
				w.done = true
				return
			if diverged:
				w.anchored = false
				if do == "" and not w.seeded:
					# physical passes where planned applies: the continuation
					# is this same beam, from here on
					w.seeded = true
					var sb = bk.seed(beam)
					if sb != beam:
						beam = sb
						skip = ci + 1
						restart = true
						break
			if do in TRANSFORMATIVE:
				var pt_exact: bool = (cand[3] != null or (rehit and fresh)) and _known_index(w.known, pt) >= 0
				var depth := w.stack.size()
				next = _apply(w, beam, t, surf, do, depart, fresh, t0, rehit, pt, pt_exact)
				e.popped = w.stack.size() < depth
				e.teleport = surf.portal or (e.popped and w.scene.by_id[next[1]].portal)
				if diverged and not w.seeded:
					w.seeded = true
					next[0] = bk.seed(next[0])
				break
			if do in PROJECTIVE:
				var pt_exact2: bool = (cand[3] != null or (rehit and fresh)) and _known_index(w.known, pt) >= 0
				next = _project(w, beam, t, surf, left, pt, pt_exact2)
				e.projective = true
				if not w.anchored and not w.seeded:
					w.seeded = true
					next[0] = bk.seed(next[0])
				break
			# the plan completed here without a frame change (an entry that
			# resolves to a pass / state change, or an unapplied projective):
			# the cursor is on this same beam -- a candidate if it lies ahead
			if not w.cursor_done and w.cursor != null and w.plan_index >= w.plan.size() and w.plan_matched and w.on_plan \
					and not cands.any(func(c): return c[1] == null):
				var tc = bk.param_of(beam, w.cursor)
				if not _before(tc, t, t0):
					var j := ci + 1
					# after everything strictly before it; ties: the cursor first
					while j < cands.size() and not cands[j][2] and _before(cands[j][0], tc, t0):
						j += 1
					cands.insert(j, [tc, null, false, null])
					gaps.insert(j, INF)
					if _known_index(w.known, w.cursor) < 0:
						w.known.append([w.cursor, tc, -1])
			ci += 1
		if restart:
			continue
		if next == null and fired_state:
			# no effect, but the loop changed the state: the next loop on the
			# same beam differs (e.g. a ghost toggler re-hit) -- walk it again
			fired_state = false
			continue
		if next == null:
			w.count += 1
			if w.count <= w.max_events:
				var t_end = null if bk.straight(beam) else t0
				var oe := _event(w, "escape" if bk.straight(beam) else "origin", beam, t_prev, t_end, INF)
				if t_end != null and fresh:
					oe.point = bk.origin(beam)
			w.done = true
			return
		fired_state = false
		beam = next[0]
		depart = next[1]
		t0 = next[2]
		fresh = next[3]
		t_prev = t0

# Apply surf's effect at t: [beam, depart, t0, fresh] to continue with.
static func _apply(w: Walk, beam, t, surf: BeamScene.Surf, etype: String, depart: int, fresh: bool, t0, rehit: bool, pt: CN,
		pt_exact: bool = false) -> Array:
	var bk = w.bk
	if not w.stack.is_empty() and w.stack.back().pop_id == surf.id and w.stack.back().etype == map_kind(surf, etype):
		var top: StackEntry = w.stack.pop_back()
		var src: BeamScene.Surf = w.scene.by_id[top.sid]
		# the point on the restored beam: T^-1(pt) for a portal, pt itself for
		# a self-inverse effect (its carrier is fixed)
		var q: CN = BeamEffects.portal_point(pt, src, true) if src.portal else pt
		var t_res
		if rehit:
			t_res = top.t_hit
		elif fresh and depart == surf.id:
			t_res = top.other
		else:
			# the shared point actually hit: one of the two (distinct) points
			var dh: float = bk.point(top.beam, top.t_hit).sub(q).abs2().to_float()
			var dd: float = bk.point(top.beam, top.other).sub(q).abs2().to_float()
			t_res = top.t_hit if dh < dd else top.other
		w.known = top.known
		w.line = top.line
		return [bk.reseed(top.beam) if w.seeded else top.beam, top.sid, t_res, false]
	var other = _other_root(w, beam, surf, depart, t0, t, pt)
	var pop_id: int = surf.pair if surf.portal else surf.id
	w.stack.append(StackEntry.make(beam, surf.id, t, other, w.known, w.line, pop_id, map_kind(surf, etype)))
	var old_known := w.known
	# the surface the new beam departs from, and the departure point
	var dsurf: BeamScene.Surf = w.scene.by_id[surf.pair] if surf.portal else surf
	var exit: CN = BeamEffects.portal_point(pt, surf) if surf.portal else pt
	var res: Array
	if w.anchored and w.chain != null:
		# anchored: the exact beam of the next plan frame; it departs dsurf at
		# the root nearest the exit point
		var nb = bk.from_hp(w.chain.beams[w.plan_index])
		var best = null
		var best_d := INF
		for r in bk.roots(nb, dsurf, false):
			var d: float = bk.point(nb, r).sub(exit).abs2().to_float()
			if d < best_d:
				best_d = d
				best = r
		if best == null:
			best = bk.param_of(nb, exit)
		res = [nb, dsurf.id, best, false]
	else:
		res = [bk.apply(beam, t, surf, etype, pt), dsurf.id, bk.zero(), true]
	if surf.portal:
		_portal_known(w, surf, pt, pt_exact, old_known, res)
		return res
	# known points of the new beam: its departure point (if exact), and the
	# old beam's known points on surf's carrier (fixed by the effect)
	w.known = [[pt, res[2], surf.id]] if pt_exact else []
	for k in old_known:
		if not BeamGeom._eq(k[0], pt) and _incident(w, k[0], surf):
			w.known.append([k[0], bk.param_of(res[0], k[0]), -1])
	# an exact line stays exact through a line reflection; through an
	# inversion only if it passes exactly through the circle's center (the
	# line maps onto itself: the new beam is exactly straight)
	if w.line != null and surf.line:
		w.line = w.line.reflected(surf.sx, surf.sy, surf.ex, surf.ey)
		_add_line_endpoints(w, res[0])
	elif w.line != null and _line_through_center(w.line, surf):
		res[0] = bk.straightened(res[0])
		w.known = w.known.filter(func(k): return k[2] == surf.id)
		_add_line_endpoints(w, res[0])
	else:
		w.line = null
		if not surf.line:
			for k in old_known:
				if _is_center(k[0], surf):
					# the old beam passes exactly through the inversion center
					# (a known point): its image is exactly a line
					res[0] = bk.straightened(res[0])
					break
	return res

# Exact: the point is the center of surf's circle.
static func _is_center(k: CN, surf: BeamScene.Surf) -> bool:
	var kx := k.fx()
	var ky := k.fy()
	if surf.full:
		# both exact float data: equality is exact
		return kx == surf.cx and ky == surf.cy
	var ec: Array = surf.exact_circle()
	var e := 2.0 * _ecc(ec)
	if absf(kx - ec[2]) > e or absf(ky - ec[3]) > e:
		return false
	var C: Array = ec[0]
	return Exact.sub(Exact.mul(Exact.of(kx), C[2]), C[0]).is_zero() \
		and Exact.sub(Exact.mul(Exact.of(ky), C[2]), C[1]).is_zero()

# Projective effect at pt: a fresh straight beam departing surf; the stack
# clears and a new epoch begins (the anchored path ends: projectives are not
# planned until Stage 87). [beam, depart, t0, fresh].
# The exact line the last projective entry j of a plan leaves along: through
# the cursor pulled back through the entries after j (when that is exact
# data), along the exact axis -- the carrier normal, the directional normal,
# or the line from the circle's exact center (oracle projective_hits).
static func _exit_line(scene: BeamScene, cursor: CN, plan: Array, flags: Dictionary, j: int):
	var X = exact_image_h(scene, cursor, plan, EXACT_IMAGE_LIMBS, flags, j + 1)
	if X == null:
		return null
	var surf: BeamScene.Surf = scene.by_id[plan[j].surface_id]
	var etype := surf.effect(plan[j].left)
	var L: Array
	if etype == "circle_normal_projection":
		# the line through the center C and X (homogeneous cross product)
		var C: Array = surf.exact_circle()[0]
		var a := Exact.sub(Exact.mul(C[1], X[2]), Exact.mul(X[1], C[2]))
		var b := Exact.sub(Exact.mul(X[0], C[2]), Exact.mul(C[0], X[2]))
		var c := Exact.sub(Exact.mul(X[0], C[1]), Exact.mul(C[0], X[1]))
		if a.is_zero() and b.is_zero():
			return null
		# L = C x X: a x + b y + c' w = 0 with c' = Cx Xy - Cy Xx = -c, i.e.
		# a x + b y = c (Exact.Line form)
		L = [a, b, c]
	else:
		var ax: float
		var ay: float
		if etype == "line_normal_projection":
			ax = -(surf.ey - surf.sy)
			ay = surf.ex - surf.sx
		else:
			var pr: Array = surf.left_proj if plan[j].left else surf.right_proj
			ax = pr[1]
			ay = pr[2]
		# through X = (x/w, y/w) along (ax, ay): -ay w x + ax w y = ax y - ay x
		var w: BigFloat = X[2]
		L = [Exact.mul(Exact.of(-ay), w), Exact.mul(Exact.of(ax), w),
			Exact.sub(Exact.mul(Exact.of(ax), X[1]), Exact.mul(Exact.of(ay), X[0]))]
	return Exact.Line.from_exact(L)

# The projective fired at its back-propagated hit point H: of the beam's
# crossings of surf, the one at pt is the one nearest H (the same position
# rule that matches a known point to its root). A plan side matched at an
# earlier crossing is not H: the anchored path ends there.
static func _at_planned_hit(w: Walk, beam, surf: BeamScene.Surf, pt: CN, H) -> bool:
	if H == null:
		return false
	# the root nearest pt is the root nearest H (distinct crossings, compared
	# as roots -- not as near-zero distances)
	var rs: Array = w.bk.roots(beam, surf, false)
	if rs.is_empty():
		return false
	var near_pt := -1
	var near_h := -1
	var d_pt := INF
	var d_h := INF
	for i in rs.size():
		var q: CN = w.bk.point(beam, rs[i])
		var a: float = q.sub(pt).abs2().to_float()
		var b: float = q.sub(H).abs2().to_float()
		if a < d_pt:
			d_pt = a
			near_pt = i
		if b < d_h:
			d_h = b
			near_h = i
	return near_pt == near_h

static func _project(w: Walk, beam, t, surf: BeamScene.Surf, left: bool, pt: CN, pt_exact: bool) -> Array:
	w.stack.clear()
	w.epoch += 1
	var at_h: bool = w.on_plan and w.chain != null and _at_planned_hit(w, beam, surf, pt, w.chain.hits.get(w.plan_index - 1))
	if not at_h:
		w.on_plan = false
	if at_h and w.anchored and w.plan_index < w.chain.beams.size():
		# planned through the projective (Stage 87): the next sub-chain's
		# anchored beam, from the back-propagated hit point; it departs the
		# surface at its root nearest the hit
		var ab = w.bk.from_hp(w.chain.beams[w.plan_index])
		var best = null
		var best_d := INF
		for r in w.bk.roots(ab, surf, false):
			var d: float = w.bk.point(ab, r).sub(pt).abs2().to_float()
			if d < best_d:
				best_d = d
				best = r
		if best == null:
			best = w.bk.param_of(ab, pt)
		w.known = [[pt, best, surf.id]] if pt_exact else []
		w.line = w.chain.exit_lines.get(w.plan_index - 1)
		if w.line != null:
			_add_line_endpoints(w, ab)
		return [ab, surf.id, best, false]
	var nb = w.bk.project(beam, t, surf, left, pt)
	w.anchored = false
	w.known = [[pt, w.bk.zero(), surf.id]] if pt_exact else []
	w.line = null
	if pt_exact:
		# an exact data line: through pt along the exact axis
		var etype := surf.effect(left)
		var pr: Array = surf.left_proj if left else surf.right_proj
		var px := pt.fx()
		var py := pt.fy()
		var L: Array
		if etype == "line_normal_projection":
			# perpendicular to the segment: (E - S) . (z - pt) = 0
			var a := Exact.sub(Exact.of(surf.ex), Exact.of(surf.sx))
			var b := Exact.sub(Exact.of(surf.ey), Exact.of(surf.sy))
			L = [a, b, Exact.add(Exact.mul(a, Exact.of(px)), Exact.mul(b, Exact.of(py)))]
		elif etype == "circle_normal_projection":
			L = Exact.line_through_h(px, py, surf.exact_circle()[0])
		else:
			L = Exact.line_from_dir(px, py, pr[1], pr[2])
		w.line = Exact.Line.from_exact(L)
		_add_line_endpoints(w, nb)
	if at_h:
		# on the planned beams (e.g. after a mirror fired at its other
		# crossing): the exit is the planned exact line (oracle planned_exit)
		var EL = w.chain.exit_lines.get(w.plan_index - 1)
		if EL != null:
			w.line = EL
			_add_line_endpoints(w, nb)
	return [nb, surf.id, w.bk.zero(), true]

# T(k) of a portal as exact data: the pair is built as T of the source, so
# endpoints map to endpoints; otherwise a translation's k + d when exactly
# representable. null: not exact data.
static func _portal_image(w: Walk, surf: BeamScene.Surf, k: CN):
	var pair: BeamScene.Surf = w.scene.by_id[surf.pair]
	var which := BeamGeom.endpoint_at(surf.seg, k)
	if which == 1:
		return pair.seg.s
	if which == 2:
		return pair.seg.e
	if surf.theta != 0.0:
		return null
	var ex := Exact.add(Exact.of(k.fx()), Exact.of(surf.dx))
	var ey := Exact.add(Exact.of(k.fy()), Exact.of(surf.dy))
	var fx := ex.to_float()
	var fy := ey.to_float()
	if Exact.of(fx).cmp(ex) != 0 or Exact.of(fy).cmp(ey) != 0:
		return null
	return _cn_like(k, fx, fy)

static func _cn_like(k: CN, x: float, y: float) -> CN:
	if k.re is BigFloat:
		var n: int = k.re.limbs.size()
		return CN.make(BigFloat.from_float(x, n), BigFloat.from_float(y, n))
	if k.re is DualNum:
		return CN.make(DualNum.of(x), DualNum.of(y))
	return CN.make(F64Num.of(x), F64Num.of(y))

# Known points and exact line after a portal push (the rigid image).
static func _portal_known(w: Walk, surf: BeamScene.Surf, pt: CN, pt_exact: bool, old_known: Array, res: Array) -> void:
	var known: Array = []
	if pt_exact:
		var h2 = _portal_image(w, surf, pt)
		if h2 != null:
			known.append([h2, res[2], surf.pair])
	for k in old_known:
		if BeamGeom._eq(k[0], pt):
			continue
		var k2 = _portal_image(w, surf, k[0])
		if k2 != null and _known_index(known, k2) < 0:
			known.append([k2, w.bk.param_of(res[0], k2), -1])
	w.known = known
	if w.line != null and surf.theta == 0.0:
		w.line = w.line.translated(surf.dx, surf.dy)
		_add_line_endpoints(w, res[0])
	else:
		w.line = null

# Exact: the current exact line is tangent to surf's circle (computed once
# per line, for every circle surface).
static func _tangent(w: Walk, surf: BeamScene.Surf) -> bool:
	if not is_same(w.tangent_line, w.line):
		w.tangent = {}
		for s in w.scene.surfaces:
			if not s.line:
				var ec: Array = s.exact_circle()
				w.tangent[s.id] = w.line.tangent_to(ec[0], ec[1], ec[2], ec[3], ec[4], _ecc(ec), 8.0 * Exact.U * absf(ec[4]))
		w.tangent_line = w.line
	return w.tangent.get(surf.id, false)

static func _line_through_center(L: Exact.Line, surf: BeamScene.Surf) -> bool:
	if surf.full:
		return L.contains(surf.cx, surf.cy)
	var ec: Array = surf.exact_circle()
	return L.contains_h(ec[0], ec[2], ec[3], _ecc(ec))

# Bound on the float rounding of an exact circle's center (C / w, rounded parts).
static func _ecc(ec: Array) -> float:
	return 6.0 * Exact.U * maxf(absf(ec[2]), absf(ec[3]))

# ---------------------------------------------------------------- helpers

# Tangent-linear seed (DualNum tier only): unit perturbation of the beam's
# tangent angle where the sensitive forward continuation starts. Every later
# derivative is then d(quantity)/d(angle) -- the amplification of a rounding
# error injected at that point. No-op for other tiers.
# Debug/validation hook: when set (a number of the solver's tier), non-dual
# tiers rotate the seeded tangent by this angle (finite-difference gains).
static var fd_seed_angle = null

static func _seed(b: HPBeam) -> HPBeam:
	if not (b.kappa is DualNum):
		if fd_seed_angle != null:
			var rot := b.tau.add(b.tau.mul_i().scale(fd_seed_angle)).unit()
			return HPBeam.make(b.p, rot, b.kappa)
		return b
	var tx: float = b.tau.re.v
	var ty: float = b.tau.im.v
	var p := CN.make(DualNum.of(b.p.re.v), DualNum.of(b.p.im.v))
	var tau := CN.make(DualNum.of(tx, -ty), DualNum.of(ty, tx))
	return HPBeam.make(p, tau, DualNum.of(b.kappa.v))

# A beam restored from the unseeded (anchored) prefix gets a tangent-linear
# seed (its own rounding error enters the continuation here).
static func _reseed_if_unseeded(b: HPBeam) -> HPBeam:
	if not (b.kappa is DualNum):
		return b
	if b.p.re.d == 0.0 and b.p.im.d == 0.0 and b.tau.re.d == 0.0 and b.tau.im.d == 0.0 and b.kappa.d == 0.0:
		return _seed(b)
	return b
