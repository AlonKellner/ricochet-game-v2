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

	func describe() -> String:
		var p := "" if point == null else " (%.6f, %.6f)" % [point.fx(), point.fy()]
		return "%s s=%d %s%s%s%s" % [kind, surface_id, "L" if left else "R", " applied" if applied else "",
			"" if on_segment else " off", p]

class PlanItem extends RefCounted:
	var surface_id: int
	var left: bool
	static func make(sid: int, p_left: bool) -> PlanItem:
		var o := PlanItem.new()
		o.surface_id = sid
		o.left = p_left
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
	static func make(p_beam, p_sid: int, p_t_hit, p_other, p_known: Array = [], p_line = null, p_pop_id: int = -1) -> StackEntry:
		var o := StackEntry.new()
		o.beam = p_beam
		o.sid = p_sid
		o.t_hit = p_t_hit
		o.other = p_other
		o.known = p_known
		o.line = p_line
		o.pop_id = p_sid if p_pop_id < 0 else p_pop_id
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

	static func _key(st: Array) -> String:
		return ",".join(st.map(func(x): return str(x)))

	# The surface whose application pops an entry pushed by sid.
	static func _pop_id(scene: BeamScene, sid: int) -> int:
		var s: BeamScene.Surf = scene.by_id.get(sid)
		return s.pair if s != null and s.portal else sid

	static func build(scene: BeamScene, player: CN, cursor: CN, plan: Array) -> AnchorChain:
		var K := plan.size()
		var stacks: Array = [[]]
		var st: Array = []
		var changes: Array = []      # plan entry j changes the frame
		for item in plan:
			var surf: BeamScene.Surf = scene.by_id.get(item.surface_id)
			var t: bool = surf != null and surf.effect(item.left) in TRANSFORMATIVE
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
	var hp_src = null         # beam whose HPBeam view is hp_view (events share it)
	var hp_view: HPBeam = null

# ---------------------------------------------------------------- entry points

# Walk an explicit direction from the player (forward shooting).
static func trace_direction(ctx: NumCtx, scene: BeamScene, player: CN, dir: CN, plan: Array, cursor, mode: int,
		max_events: int = MAX_HITS) -> Array:
	return run_direction(NumBackend.make(ctx), scene, player, dir, plan, cursor, mode, max_events).events

static func run_direction(bk, scene: BeamScene, player: CN, dir: CN, plan: Array, cursor, mode: int,
		max_events: int = MAX_HITS) -> Walk:
	var w := _new_walk(bk, scene, plan, cursor, mode, max_events)
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
static func planned(ctx: NumCtx, scene: BeamScene, player: CN, cursor: CN, plan: Array, max_events: int = MAX_HITS) -> Array:
	var w := run(NumBackend.make(ctx), scene, player, cursor, plan, Mode.PLANNED, max_events)
	return [] if w == null else w.events

# Physical trace: on the anchored planned path until the first divergence.
static func physical(ctx: NumCtx, scene: BeamScene, player: CN, cursor: CN, plan: Array, max_events: int = MAX_HITS) -> Array:
	var w := run(NumBackend.make(ctx), scene, player, cursor, plan, Mode.PHYSICAL, max_events)
	return [] if w == null else w.events

# null if the anchored construction is degenerate (player = cursor, or a pole
# / image at infinity: Stage 83).
static func run(bk, scene: BeamScene, player: CN, cursor: CN, plan: Array, mode: int, max_events: int = MAX_HITS) -> Walk:
	var chain := AnchorChain.build(scene, player, cursor, plan)
	if chain == null:
		return _run_degenerate(bk, scene, player, cursor, plan, mode, max_events)
	var w := _new_walk(bk, scene, plan, cursor, mode, max_events)
	w.chain = chain
	w.anchored = true
	var b0 = bk.from_hp(chain.beams[0])
	if mode == Mode.PHYSICAL and plan.is_empty():
		# no plan: the physical trace is the forward continuation from the start
		b0 = bk.seed(b0)
		w.seeded = true
		w.anchored = false
	w.known = [[player, bk.zero(), -1]]
	w.line = _initial_line(scene, player, cursor, plan)
	if w.line != null:
		_add_line_endpoints(w, b0)
	walk_from(w, b0, -1, null)
	return w

# The exact implicit line of B_0, when it is exact data: the line player ->
# F_K^-1(cursor) with every frame change of the plan a line reflection (the
# image is then an exact rational point, carried homogeneously). null if the
# plan has an inversion.
static func _initial_line(scene: BeamScene, player: CN, cursor: CN, plan: Array):
	var X: Array = [Exact.of(cursor.fx()), Exact.of(cursor.fy()), Exact.of(1.0)]
	for j in range(plan.size() - 1, -1, -1):
		var surf: BeamScene.Surf = scene.by_id.get(plan[j].surface_id)
		if surf == null or not (surf.effect(plan[j].left) in TRANSFORMATIVE):
			continue
		if surf.portal:
			if surf.theta != 0.0:
				return null
			# T^-1(X) = X - d (homogeneous: X - d w)
			X = [Exact.sub(X[0], Exact.mul(Exact.of(surf.dx), X[2])), Exact.sub(X[1], Exact.mul(Exact.of(surf.dy), X[2])), X[2]]
			continue
		if not surf.line:
			return null
		X = Exact.reflect_point_h(X, surf.sx, surf.sy, surf.ex, surf.ey)
	return Exact.Line.from_exact(Exact.line_through_h(player.fx(), player.fy(), X))

# A point at infinity in the anchored construction (player / cursor exactly at
# an inversion center): forward walk from the exact aim direction, or -- when
# the aim image itself is at infinity (the aim is undetermined) -- straight at
# the cursor (oracle trace2._aim). null only for player = cursor.
static func _run_degenerate(bk, scene: BeamScene, player: CN, cursor: CN, plan: Array, mode: int, max_events: int) -> Walk:
	var X = cursor
	for j in range(plan.size() - 1, -1, -1):
		var surf: BeamScene.Surf = scene.by_id.get(plan[j].surface_id)
		if surf == null or not (surf.effect(plan[j].left) in TRANSFORMATIVE):
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
		line = _initial_line(scene, player, cursor, plan)
	var w := _new_walk(bk, scene, plan, cursor, mode, max_events)
	var b0 = bk.from_hp(HPBeam.make(player, dir.unit(), player.re.sub(player.re)))
	if mode == Mode.PHYSICAL:
		b0 = bk.seed(b0)
		w.seeded = true
	w.known = [[player, bk.zero(), -1]]
	w.line = line
	if w.line != null:
		_add_line_endpoints(w, b0)
	walk_from(w, b0, -1, null)
	return w

static func _new_walk(bk, scene: BeamScene, plan: Array, cursor, mode: int, max_events: int) -> Walk:
	var w := Walk.new()
	w.bk = bk
	w.scene = scene
	w.plan = plan
	w.cursor = cursor
	w.mode = mode
	w.post_mode = Mode.PHYSICAL
	w.max_events = max_events
	return w

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
	if not w.cursor_done and w.plan_index >= w.plan.size() and w.plan_matched and w.cursor != null:
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
static func _other_root(w: Walk, beam, surf: BeamScene.Surf, depart: int, t0, t):
	if surf.id == depart:
		return t0
	for r in w.bk.roots(beam, surf, false):
		if r.cmp(t) != 0:
			return r
	return t   # tangent / straight beam: a single root

# Forward walk. depart: id of the surface the beam departs from (-1: none).
# t_start null: fresh (the beam's own point p is the departure, t = 0);
# otherwise resumed at t_start.
static func walk_from(w: Walk, beam, depart: int, t_start) -> void:
	var bk = w.bk
	var fresh: bool = t_start == null
	var t0 = bk.zero() if fresh else t_start
	var skip := 0          # candidates already processed (in-place reseed)
	var t_prev = t0
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
			var eff := surf.effect(left)
			var plan_match: bool = w.plan_index < w.plan.size() and w.plan[w.plan_index].surface_id == surf.id \
				and w.plan[w.plan_index].left == left
			var plan_do: String = (eff if eff in TRANSFORMATIVE else "") if plan_match else ""
			var phys_do: String = eff if full_block else ""
			var do := ""
			var diverged := false
			if w.mode == Mode.PHYSICAL:
				do = phys_do
				if w.anchored and not (phys_do == plan_do and (phys_do != "" or not plan_match)):
					diverged = true
				if do in TRANSFORMATIVE or do in PROJECTIVE:
					_plan_bookkeeping(w, surf.id)
			else:
				if plan_match:
					w.plan_index += 1
				do = plan_do
			var e := _event(w, "hit", beam, t_prev, t, gaps[ci])
			e.surface_id = surf.id
			e.left = left
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
				if not w.seeded:
					w.seeded = true
					next[0] = bk.seed(next[0])
				break
			ci += 1
		if restart:
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
		beam = next[0]
		depart = next[1]
		t0 = next[2]
		fresh = next[3]
		t_prev = t0

# Apply surf's effect at t: [beam, depart, t0, fresh] to continue with.
static func _apply(w: Walk, beam, t, surf: BeamScene.Surf, etype: String, depart: int, fresh: bool, t0, rehit: bool, pt: CN,
		pt_exact: bool = false) -> Array:
	var bk = w.bk
	if not w.stack.is_empty() and w.stack.back().pop_id == surf.id:
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
	var other = _other_root(w, beam, surf, depart, t0, t)
	var pop_id: int = surf.pair if surf.portal else surf.id
	w.stack.append(StackEntry.make(beam, surf.id, t, other, w.known, w.line, pop_id))
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
static func _project(w: Walk, beam, t, surf: BeamScene.Surf, left: bool, pt: CN, pt_exact: bool) -> Array:
	var nb = w.bk.project(beam, t, surf, left, pt)
	w.stack.clear()
	w.epoch += 1
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
