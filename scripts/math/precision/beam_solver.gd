class_name BeamSolver
extends RefCounted

# Precision-generic trajectory solver for scenes of full inversion circles
# (plus effect-free pass-through surfaces, which never change the beam).
#
# Planned trace: cursor-anchored construction. Beam B_j (after j plan
# entries) is the circle through P_j = F_j(player), X_j = F_j(F_K^-1(cursor))
# and Q_j = F_j(inf), oriented P -> X -> Q. P_j and Q_j are pushed forward and
# X_j pulled back from the cursor, all by contractive maps, and P_j - Q_j is
# tracked as an exact difference. Each B_j is built independently, so errors
# never accumulate along the chain: exact to ~1e-13 px in float64 at any depth.
#
# Physical trace: identical to the planned path up to the first planned pass
# event (physical applies it), then a forward continuation, which is
# genuinely sensitive and is run on a precision ladder (see PrecisionLadder).
#
# Provenance (no tolerances): applying the inversion of the surface whose
# inversion produced the current beam cancels it (I_S o I_S = id, Tracer's
# source-based stack pop). The earlier beam is restored EXACTLY (same object)
# and the trace resumes at its other recorded intersection with S -- which is,
# by definition, the same shared point (B ∩ S = I_S(B) ∩ S). Nothing is
# recomputed, so e.g. a straight beam stays exactly straight.

enum Mode { PHYSICAL = 0, PLANNED = 1 }

class Event extends RefCounted:
	var kind: String          # "hit" | "cursor" | "origin" | "escape"
	var surface_id: int = -1
	var left: bool = false    # side: true = LEFT (beam enters the circle)
	var applied: bool = false
	var point: CN = null      # null for escape
	var beam: HPBeam          # beam the arc leading to this event lies on
	var t_from                # arc start parameter on `beam`
	var t_to                  # arc end parameter on `beam` (null = infinity)
	var margin: float = INF   # px gap to the nearest competing candidate of the
	                          # ordering/root decision that produced this event
	var seeded: bool = false  # lies on the sensitive forward continuation (the
	                          # beam chain starting at the tangent-linear seed)
	var raw = null            # FastSolver: raw float beam of `beam` (provenance)

	func describe() -> String:
		var p := "" if point == null else " (%.6f, %.6f)" % [point.fx(), point.fy()]
		return "%s s=%d %s%s%s" % [kind, surface_id, "L" if left else "R", " applied" if applied else "", p]

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
	var beam                  # HPBeam (or FastSolver raw beam)
	var sid: int
	var t_hit
	var other
	static func make(p_beam, p_sid: int, p_t_hit, p_other) -> StackEntry:
		var o := StackEntry.new()
		o.beam = p_beam
		o.sid = p_sid
		o.t_hit = p_t_hit
		o.other = p_other
		return o

# ---------------------------------------------------------------- ordering

# Projective order along a beam starting at t0: true if a comes before b.
static func _before(a, b, t0) -> bool:
	var ka: int = 0 if a.cmp(t0) > 0 else 1
	var kb: int = 0 if b.cmp(t0) > 0 else 1
	if ka != kb:
		return ka < kb
	return a.cmp(b) < 0

# Roots of `beam` ∩ `c` that are candidates for the next event.
#   fresh: the beam's own point p (t = 0) is the departure point, exactly on
#          the departure surface (hits() then uses the exact P = 0 form).
#   not fresh: the trace sits at parameter t0 on the beam; on the departure
#          surface the root equal to t0 (same computation -> same value) is
#          the departure itself.
static func _roots(beam: HPBeam, c: HPBeam.Circle, fresh: bool, depart_id: int, t0) -> Array:
	if fresh:
		var rs := beam.hits(c, depart_id == c.id)
		return rs.filter(func(t): return not t.is_zero())
	var rs2 := beam.hits(c, false)
	if c.id == depart_id:
		return rs2.filter(func(t): return t.cmp(t0) != 0)
	return rs2

# Cancelling re-hit of the surface of stack entry `top` at parameter t on
# `cur`. beam_old ∩ S = cur ∩ S = {h (where top was applied), h'}; resume
# beam_old at whichever of the two this hit is. If cur was born at h (fresh,
# departing S) its departure root is excluded, so the hit is h' by
# construction; otherwise identify the point (the two are distinct).
static func _resume_param(top: StackEntry, cur: HPBeam, t, born_at_hit: bool):
	if born_at_hit:
		return top.other
	var p := cur.at(t)
	var old: HPBeam = top.beam
	var d_hit: float = old.at(top.t_hit).sub(p).abs2().to_float()
	var d_other: float = old.at(top.other).sub(p).abs2().to_float()
	return top.t_hit if d_hit < d_other else top.other

# The other intersection of `beam` with c, given the one we apply at (t).
static func _other_root(beam: HPBeam, c: HPBeam.Circle, _fresh: bool, depart_id: int, t0, t):
	if c.id == depart_id:
		# beam ∩ c = {departure point, t}
		return t0
	for r in beam.hits(c, false):
		if r.cmp(t) != 0:
			return r
	return t  # tangent: a single (double) root

# Anchored beams (not built at a hit point): the departure root is either
# known exactly (after a cancelling pop: dep_t) or identified as the root
# whose point is the departure point (B ∩ S shares both points with the
# previous beam ∩ S; the nearer one is the departure).
static func _roots_anchored(B: HPBeam, c: HPBeam.Circle, dep_id: int, dep_t, dep_pt: CN) -> Array:
	var ts := B.hits(c, false)
	if c.id != dep_id or ts.is_empty():
		return ts
	if dep_t != null:
		return ts.filter(func(t): return t.cmp(dep_t) != 0)
	var best := 0
	var best_d := INF
	for i in ts.size():
		var d: float = B.at(ts[i]).sub(dep_pt).abs2().to_float()
		if d < best_d:
			best_d = d
			best = i
	ts.remove_at(best)
	return ts

# The root of B ∩ c at the departure point (B ∩ c shares both points with the
# previous beam ∩ c; the one at the departure point is the nearer).
static func _departure_root(B: HPBeam, c: HPBeam.Circle, dep_pt: CN):
	var best = null
	var best_d := INF
	for t in B.hits(c, false):
		var d: float = B.at(t).sub(dep_pt).abs2().to_float()
		if d < best_d:
			best_d = d
			best = t
	return best

# Per candidate: px distance to its neighbours in the sorted order (an
# ordering decision is only as reliable as this gap is large vs. the error;
# used by ErrorEstimate to decide precision, never to decide topology).
static func _candidate_gaps(beam: HPBeam, cands: Array) -> Array:
	var pts: Array = []
	for cand in cands:
		pts.append(_point_f64(beam, cand[0]))
	var gaps: Array = []
	for i in cands.size():
		var g := INF
		# surface candidates only: the cursor is not an effect, its order
		# relative to a coincident hit changes nothing
		if cands[i][1] != null:
			var l := i - 1
			while l >= 0 and cands[l][1] == null:
				l -= 1
			if l >= 0:
				g = minf(g, _dist(pts[i], pts[l]))
			var r := i + 1
			while r < cands.size() and cands[r][1] == null:
				r += 1
			if r < cands.size():
				g = minf(g, _dist(pts[i], pts[r]))
		gaps.append(g)
	return gaps

# float64 distance between two points of any tier
static func _dist(a: CN, b: CN) -> float:
	var dx: float = a.fx() - b.fx()
	var dy: float = a.fy() - b.fy()
	return sqrt(dx * dx + dy * dy)

static func _is_hp(beam: HPBeam) -> bool:
	return beam.kappa is BigFloat

static func _f64_beam(beam: HPBeam) -> PackedFloat64Array:
	return FastSolver.beam(beam.p.fx(), beam.p.fy(), beam.tau.fx(), beam.tau.fy(), beam.kappa.to_float())

# float64 view of a point (for margins / display only)
static func _point_f64(B: HPBeam, t) -> CN:
	if not _is_hp(B):
		return B.at(t)
	var h := FastSolver.at(_f64_beam(B), t.to_float(), 0.0)
	return CN.make(F64Num.of(h[0]), F64Num.of(h[1]))

# ---------------------------------------------------------------- planned

static func _stack_key(st: Array) -> String:
	return ",".join(st.map(func(x): return str(x)))

# stop_after_first_pass: return as soon as the first pass event is emitted
# (all a physical trace needs from the planned path).
static func anchored_planned(ctx: NumCtx, circles: Array, player: CN, cursor: CN, plan: Array, post_events: int = 5, stop_after_first_pass: bool = false) -> Array:
	var by_id := {}
	for c in circles:
		by_id[c.id] = c
	var K := plan.size()
	# transform stacks (immediate repeat of the same surface pops, like Tracer)
	var stacks: Array = [[]]
	var st: Array = []
	for item in plan:
		if not st.is_empty() and st.back() == item.surface_id:
			st.pop_back()
		else:
			st.append(item.surface_id)
		stacks.append(st.duplicate())
	# pole chain: key -> [P, Q or null, delta or null]
	var memo := {"": [player, null, null]}
	for s in stacks:
		for n in range(1, s.size() + 1):
			var key := _stack_key(s.slice(0, n))
			if memo.has(key):
				continue
			var parent: Array = memo[_stack_key(s.slice(0, n - 1))]
			var circ: HPBeam.Circle = by_id[s[n - 1]]
			memo[key] = _push_pole(parent, circ)
	# image chain X_j, memoized by frame (exact provenance across pops)
	var X: Array = []
	X.resize(K + 1)
	var seen := {}
	for j in range(K, -1, -1):
		var key := _stack_key(stacks[j])
		if seen.has(key):
			X[j] = seen[key]
		elif j == K:
			X[j] = cursor
		else:
			X[j] = HPBeam.invert_point(X[j + 1], by_id[plan[j].surface_id])
		seen[key] = X[j]
	# one beam object per frame (a popped frame IS the earlier beam object)
	var beam_of := {}
	var beams: Array = []
	for j in K + 1:
		var key := _stack_key(stacks[j])
		if not beam_of.has(key):
			var pc: Array = memo[key]
			beam_of[key] = HPBeam.through(pc[0], X[j], pc[2])
		beams.append(beam_of[key])

	var events: Array = []
	var astack: Array = []        # StackEntry, mirrors `stacks`
	var t_start = ctx.zero()
	var dep_id := -1
	var dep_t = null              # exact departure parameter (after a pop)
	var dep_pt: CN = null
	for j in K + 1:
		var B: HPBeam = beams[j]
		var t_end
		var end_circ: HPBeam.Circle = null
		if j < K:
			end_circ = by_id[plan[j].surface_id]
			var cand: Array = []
			for t in _roots_anchored(B, end_circ, dep_id, dep_t, dep_pt):
				if B.is_entering(t, end_circ) == plan[j].left:
					cand.append(t)
			if cand.is_empty():
				# The plan cannot be followed (e.g. a cancelling pair aims straight
				# at the cursor and the line misses the surface). Planned mode then
				# applies nothing more: the beam passes every surface and the trace
				# ends; the cursor is never reached (Tracer semantics).
				var t_dep = null   # null: fresh start at B's own point (the player)
				if j > 0:
					t_dep = dep_t if dep_t != null else _departure_root(B, by_id[dep_id], dep_pt)
				events.append_array(continue_from(ctx, circles, B, dep_id, plan, j, false, null, true,
					Mode.PLANNED, Mode.PLANNED, post_events + 64, false, astack, t_dep))
				return events
			t_end = cand[0]
			for t in cand:
				if _before(t, t_end, t_start):
					t_end = t
		else:
			t_end = B.param_of(cursor)
		# pass events strictly between t_start and t_end
		var passes: Array = []
		for c in circles:
			for t in _roots_anchored(B, c, dep_id, dep_t, dep_pt):
				if c == end_circ and t.cmp(t_end) == 0:
					continue
				if t.cmp(t_start) != 0 and _before(t, t_end, t_start):
					passes.append([t, c])
		passes.sort_custom(func(a, b): return _before(a[0], b[0], t_start))
		# decision margins (precision control only)
		var end_pt := B.at(t_end)
		var end_margin := INF
		if end_circ != null:
			for t in B.hits(end_circ, false):
				if t.cmp(t_end) != 0:
					end_margin = minf(end_margin, _dist(_point_f64(B, t), end_pt))
		if end_circ != null:
			for pr in passes:
				end_margin = minf(end_margin, _dist(_point_f64(B, pr[0]), end_pt))
		var t_prev = t_start
		for pr in passes:
			events.append(_event("hit", pr[1].id, B.is_entering(pr[0], pr[1]), false, B, t_prev, pr[0]))
			events.back().margin = _dist(events.back().point, end_pt) if end_circ != null else INF
			t_prev = pr[0]
			if stop_after_first_pass:
				return events
		if j < K:
			var e := _event("hit", plan[j].surface_id, plan[j].left, true, B, t_prev, t_end)
			e.margin = end_margin
			events.append(e)
			var sid: int = plan[j].surface_id
			if not astack.is_empty() and astack.back().sid == sid:
				# cancelling re-hit: resume the earlier beam at its other root
				var top: StackEntry = astack.pop_back()
				t_start = top.other
				dep_id = sid
				dep_t = top.other
				dep_pt = null
			else:
				var other = null
				for r in B.hits(end_circ, false):
					if r.cmp(t_end) != 0:
						other = r
				astack.append(StackEntry.make(B, sid, t_end, other if other != null else t_end))
				dep_id = sid
				dep_t = null
				dep_pt = e.point
				t_start = beams[j + 1].param_of(dep_pt)
		else:
			events.append(_event("cursor", -1, false, false, B, t_prev, t_end))
			events.back().margin = end_margin
			if post_events > 0:
				var cur := _seed(B.rebased_at(t_end))
				events.append_array(continue_from(ctx, circles, cur, -1, plan, K, true, null, true,
					Mode.PHYSICAL, Mode.PHYSICAL, post_events, true, astack.duplicate()))
	return events

static func _push_pole(parent: Array, circ: HPBeam.Circle) -> Array:
	var P0: CN = parent[0]
	var Q0 = parent[1]
	var d0 = parent[2]
	var P1 := HPBeam.invert_point(P0, circ)
	if Q0 == null:
		var u := P0.sub(circ.c)
		return [P1, circ.c, u.scale(circ.r2).div_real(u.abs2())]
	var uq: CN = Q0.sub(circ.c)
	if uq.is_zero():
		return [P1, null, null]
	var Q1 := HPBeam.invert_point(Q0, circ)
	# delta' = -R^2 conj(delta / ((P0 - c)(Q0 - c)))
	var d1: CN = d0.div(P0.sub(circ.c).mul(uq)).conj().scale(circ.r2).neg()
	return [Q1.add(d1), Q1, d1]

static func _event(kind: String, sid: int, left: bool, applied: bool, beam: HPBeam, t_from, t_to) -> Event:
	var e := Event.new()
	e.kind = kind
	e.surface_id = sid
	e.left = left
	e.applied = applied
	e.beam = beam
	e.t_from = t_from
	e.t_to = t_to
	if t_to != null:
		e.point = beam.at(t_to)
	return e

# ---------------------------------------------------------------- forward

# Forward trace. The trace starts on `beam` either at its own point p
# (t_start == null: "fresh", p exactly on surface depart_id or on none) or at
# parameter t_start (after a cancelling pop). `stack` holds the transform
# stack entries (StackEntry) of the beams that led here.
static func continue_from(ctx: NumCtx, circles: Array, beam: HPBeam, depart_id: int, plan: Array,
		plan_index: int, plan_matched: bool, cursor, cursor_done: bool, mode: int, post_mode: int,
		max_events: int, seeded: bool = true, stack: Array = [], t_start = null) -> Array:
	var events: Array = []
	var cur_mode := mode
	var fresh: bool = t_start == null
	var t0 = ctx.zero() if fresh else t_start
	while events.size() < max_events:
		var cands: Array = []
		for c in circles:
			for t in _roots(beam, c, fresh, depart_id, t0):
				cands.append([t, c])
		if not cursor_done and plan_index >= plan.size() and plan_matched and cursor != null:
			cands.append([beam.param_of(cursor), null])
		var t_origin = t0
		cands.sort_custom(func(a, b): return _before(a[0], b[0], t_origin))
		var gaps := _candidate_gaps(beam, cands)
		var applied := false
		var t_prev = t0
		for ci in cands.size():
			var cand: Array = cands[ci]
			if events.size() >= max_events:
				break
			var t = cand[0]
			var c: HPBeam.Circle = cand[1]
			if c == null:
				events.append(_event("cursor", -1, false, false, beam, t_prev, t))
				events.back().margin = gaps[ci]
				events.back().seeded = seeded
				t_prev = t
				cursor_done = true
				cur_mode = post_mode
				continue
			var left := beam.is_entering(t, c)
			var do_apply := false
			if cur_mode == Mode.PHYSICAL:
				do_apply = true
				if plan_index < plan.size():
					if c.id == plan[plan_index].surface_id:
						plan_index += 1
					else:
						plan_matched = false
				else:
					plan_matched = false
			elif plan_index < plan.size() and c.id == plan[plan_index].surface_id and left == plan[plan_index].left:
				do_apply = true
				plan_index += 1
			events.append(_event("hit", c.id, left, do_apply, beam, t_prev, t))
			events.back().margin = gaps[ci]
			events.back().seeded = seeded
			t_prev = t
			if do_apply:
				if not stack.is_empty() and stack.back().sid == c.id:
					var top: StackEntry = stack.pop_back()
					t0 = _resume_param(top, beam, t, fresh and depart_id == c.id)
					beam = _reseed_if_unseeded(top.beam)
					fresh = false
				else:
					var other = _other_root(beam, c, fresh, depart_id, t0, t)
					stack.append(StackEntry.make(beam, c.id, t, other))
					beam = beam.invert_at(t, c)
					t0 = ctx.zero()
					fresh = true
				depart_id = c.id
				applied = true
				break
		if not applied:
			if events.size() < max_events:
				if beam.kappa.is_zero():
					events.append(_event("escape", -1, false, false, beam, t_prev, null))
				else:
					events.append(_event("origin", -1, false, false, beam, t_prev, t0))
				events.back().seeded = seeded
			break
	return events

# ---------------------------------------------------------------- physical

static func physical(ctx: NumCtx, circles: Array, player: CN, cursor: CN, plan: Array, max_events: int) -> Array:
	if plan.is_empty():
		var dir := cursor.sub(player)
		var b0 := _seed(HPBeam.make(player, dir.unit(), ctx.zero()))
		return continue_from(ctx, circles, b0, -1, plan, 0, true, cursor, false,
			Mode.PHYSICAL, Mode.PHYSICAL, max_events)
	var planned := anchored_planned(ctx, circles, player, cursor, plan, max_events, true)
	if planned.is_empty():
		return []
	var by_id := {}
	for c in circles:
		by_id[c.id] = c
	var out: Array = []
	var plan_index := 0
	var stack: Array = []
	for e in planned:
		if out.size() >= max_events:
			break
		if e.kind == "hit" and not e.applied:
			# first divergence: physical applies what planned passes through
			var circ: HPBeam.Circle = by_id[e.surface_id]
			var d := _event("hit", e.surface_id, e.left, true, e.beam, e.t_from, e.t_to)
			d.margin = e.margin
			out.append(d)
			var matched: bool = plan_index < plan.size() and plan[plan_index].surface_id == e.surface_id
			var rest: Array
			if not stack.is_empty() and stack.back().sid == e.surface_id:
				var top: StackEntry = stack.pop_back()
				var t_res = _resume_param(top, e.beam, e.t_to, false)
				rest = continue_from(ctx, circles, _seed(top.beam), e.surface_id, plan,
					plan_index + (1 if matched else 0), matched, cursor, false,
					Mode.PHYSICAL, Mode.PHYSICAL, max_events - out.size(), true, stack, t_res)
			else:
				var other = null
				for r in e.beam.hits(circ, false):
					if r.cmp(e.t_to) != 0:
						other = r
				stack.append(StackEntry.make(e.beam, e.surface_id, e.t_to, other if other != null else e.t_to))
				var nb: HPBeam = _seed(e.beam.invert_at(e.t_to, circ))
				rest = continue_from(ctx, circles, nb, e.surface_id, plan,
					plan_index + (1 if matched else 0), matched, cursor, false,
					Mode.PHYSICAL, Mode.PHYSICAL, max_events - out.size(), true, stack)
			out.append_array(rest)
			return out
		out.append(e)
		if e.kind == "hit":
			plan_index += 1
			_track(stack, e, by_id)
	return out

# Mirror the transform stack along applied planned events.
static func _track(stack: Array, e: Event, by_id: Dictionary) -> void:
	if not stack.is_empty() and stack.back().sid == e.surface_id:
		stack.pop_back()
		return
	var other = null
	for r in e.beam.hits(by_id[e.surface_id], false):
		if r.cmp(e.t_to) != 0:
			other = r
	stack.append(StackEntry.make(e.beam, e.surface_id, e.t_to, other if other != null else e.t_to))

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

# FastSolver twin: a beam restored from the unseeded (planned) prefix gets a
# tangent-linear seed (its own rounding error enters the continuation here).
static func _reseed_if_unseeded(b: HPBeam) -> HPBeam:
	if not (b.kappa is DualNum):
		return b
	if b.p.re.d == 0.0 and b.p.im.d == 0.0 and b.tau.re.d == 0.0 and b.tau.im.d == 0.0 and b.kappa.d == 0.0:
		return _seed(b)
	return b

static func make_circles(ctx: NumCtx, defs: Array) -> Array:
	# defs: [[id, cx, cy, r], ...] (exact float64 game data)
	var out: Array = []
	for d in defs:
		out.append(HPBeam.Circle.make(d[0], ctx.cn(d[1], d[2]), ctx.num(d[3])))
	return out
