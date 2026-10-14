class_name GdErrorEstimate
extends RefCounted

# Accumulated-error estimation for GdBeamSolver traces.
#
# A priori (tangent-linear): GdBeamSolver runs over GdDualNum with a unit seed on
# the beam's tangent angle where the sensitive forward continuation starts
# (divergence point / cursor / player). The derivative magnitude G_i =
# |d p_i / d phi| is the amplification of a rounding error injected there.
# Rounding is injected at every step, though, and a perturbation amplified at
# a peak is not "un-amplified" when the beam later retraces (a palindromic
# path shrinks the seeded perturbation back, but not errors injected at the
# peak in other directions). Hence the running maximum (envelope):
#     est_i(tier) = C * eps(tier) * max_{j <= i} G_j
# Events before the seed (anchored planned prefix) do not amplify: their
# error is ~eps * scale, estimated by GdPrecisionLadder.planned_residuals.
# C is calibrated against the oracle corpus (bench: est >= actual).
#
# A posteriori: distance between two tiers (GdPrecisionLadder.event_distance).

const C := 4096.0   # calibrated on the oracle corpus: 98.8% coverage, no missed escalations
const SCALE := 4096.0   # coordinate magnitude bound for the unamplified floor
const MARGIN_SAFETY := 16.0

class Sensitivity extends RefCounted:
	var gain: Array = []      # per event G_i (0 where not seeded)
	var max_gain: float = 0.0
	var events: Array = []    # the f64 (dual value) events
	# First event whose ordering/root decision margin is not comfortably larger
	# than its float64 error estimate: beyond it the float64 trajectory may be
	# on a different branch and its gains say nothing about the true one.
	var unreliable_from: int = -1

# GdFastSolver computes the float64 trace and the tangent-linear gains in one
# raw-float pass (bit-identical to GdBeamSolver over GdDualNum values).
# defs: scene defs (GdBeamScene).
# fsc: the float64 scene of defs, when the caller already has it.
static func physical_sensitivity(defs: Array, player: Array, cursor: Array, plan: Array, max_events: int,
		fsc: GdFastSolver.Scene = null, flags: Dictionary = {}) -> Sensitivity:
	var sc := fsc if fsc != null else GdFastSolver.Scene.from_defs(defs)
	var out := GdFastSolver.physical(sc, PackedFloat64Array(player), PackedFloat64Array(cursor), plan, max_events, flags)
	return _collect(out.events, out.gain)

static func planned_sensitivity(defs: Array, player: Array, cursor: Array, plan: Array, max_events: int,
		fsc: GdFastSolver.Scene = null, flags: Dictionary = {}) -> Sensitivity:
	var sc := fsc if fsc != null else GdFastSolver.Scene.from_defs(defs)
	var out := GdFastSolver.planned(sc, PackedFloat64Array(player), PackedFloat64Array(cursor), plan, max_events, flags)
	return _collect(out.events, out.gain)

static func _collect(ev: Array, gains: Array) -> Sensitivity:
	var s := Sensitivity.new()
	s.events = ev
	var envelope := 0.0
	for i in ev.size():
		envelope = maxf(envelope, gains[i])
		s.gain.append(envelope)
	s.max_gain = envelope
	var est := estimate(s, GdNumCtx.f64().eps())
	for i in ev.size():
		if ev[i].margin <= MARGIN_SAFETY * est[i]:
			s.unreliable_from = i
			break
	return s

# Tangent-linear gains along an arbitrary-precision trajectory, computed in
# float64: the dual beam's VALUES are reset at every step to the float64
# rounding of the high-precision beam, while the DERIVATIVES keep propagating.
# Derivatives are smooth in the state, so this rounding does not matter, and
# the gains describe the true (high-precision) branch -- unlike gains from the
# float64 trajectory, which may have taken a different branch.
static func replay(events: Array, defs: Array, fsc: GdFastSolver.Scene = null) -> Sensitivity:
	var scene: GdBeamScene = fsc.scene if fsc != null else GdBeamScene.from_defs(GdNumCtx.f64(), defs)
	var s := Sensitivity.new()
	s.events = events
	var db = null                # dual beam (PackedFloat64Array) or null before the seed
	var cur_beam: GdHPBeam = null
	var envelope := 0.0
	for e in events:
		var g := 0.0
		if e.seeded:
			if db == null or e.beam != cur_beam:
				var b: GdHPBeam = e.beam
				var vals := GdFastSolver.beam(b.p.fx(), b.p.fy(), b.tau.fx(), b.tau.fy(), b.kappa.to_float())
				if db == null:
					db = GdFastSolver.seeded(vals)
				else:
					for k in 5:
						db[k] = vals[k]
				cur_beam = e.beam
			if e.point != null and e.t_to != null:
				var tf: float = e.t_to.to_float()
				var dt := 0.0
				var surf: GdBeamScene.Surf = scene.by_id.get(e.surface_id) if e.kind == "hit" else null
				if surf != null:
					# the root this event is (nearest in t), with its derivative
					var hs := GdFastSolver.surf_hits(db, surf, false)
					var best := INF
					var j := 0
					while j < hs.size():
						if hs[j] == tf:
							dt = hs[j + 1]
							break
						if absf(hs[j] - tf) < best:
							best = absf(hs[j] - tf)
							dt = hs[j + 1]
						j += 2
				var h := GdFastSolver.at(db, tf, dt)
				g = sqrt(h[2] * h[2] + h[3] * h[3])
				if e.kind == "hit" and e.applied and not e.terminal and surf != null:
					db = GdFastSolver.apply_surf(db, tf, dt, surf)
		envelope = maxf(envelope, g)
		s.gain.append(envelope)
	s.max_gain = envelope
	return s

# Reliability of a trajectory at a given tier: first event whose decision
# margin is not comfortably larger than its estimated error (-1 = reliable).
static func unreliable_index(sens: Sensitivity, ctx_eps: float) -> int:
	var est := estimate(sens, ctx_eps)
	for i in sens.events.size():
		if sens.events[i].margin <= MARGIN_SAFETY * est[i]:
			return i
	return -1

static func estimate(sens: Sensitivity, ctx_eps: float) -> Array:
	var out: Array = []
	var floor_err := ctx_eps * SCALE * 4.0
	for g in sens.gain:
		out.append(maxf(C * ctx_eps * g, floor_err))
	return out

# Smallest ladder tier (limbs; 0 = float64) whose a-priori estimate is <= tol.
static func required_limbs(sens: Sensitivity, tol: float, tiers: Array) -> int:
	for limbs in tiers:
		var eps := GdNumCtx.with_limbs(limbs).eps()
		if C * eps * sens.max_gain <= tol and eps * SCALE * 4.0 <= tol:
			return limbs
	return tiers.back()
