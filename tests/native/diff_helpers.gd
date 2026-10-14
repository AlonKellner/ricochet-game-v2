extends RefCounted
## Bit-identity helpers of the differential tests (GDScript class vs its native
## twin). Every function returns "" when the two values are identical and a
## one-line description of the first difference otherwise, so a test can report
## exactly where two traces part. Floats compare by bit pattern (-0.0 and NaN
## payloads included), BigFloats by sign / exponent / limbs.


static func bits(f: float) -> PackedByteArray:
	return PackedFloat64Array([f]).to_byte_array()


static func same_float(a: float, b: float) -> bool:
	return bits(a) == bits(b)


static func hex(f: float) -> String:
	var b := bits(f)
	b.reverse()
	return b.hex_encode()


## GDScript scalar (GdF64Num / GdDualNum / GdBigFloat / null) vs Num (or null).
static func num_diff(a, b, label: String) -> String:
	if a == null or b == null:
		return "" if a == null and b == null else "%s: %s vs %s" % [label, a, b]
	if a is GdBigFloat:
		if a.sign != b.sign or a.exp != b.exp or a.limbs != b.get_limbs():
			return "%s: bigfloat (%d, %d, %s) vs (%d, %d, %s)" % [label, a.sign, a.exp, a.limbs, b.sign, b.exp, b.get_limbs()]
		return ""
	if a is GdDualNum:
		if not same_float(a.v, b.v) or not same_float(a.d, b.d):
			return "%s: dual (%s, %s) vs (%s, %s)" % [label, hex(a.v), hex(a.d), hex(b.v), hex(b.d)]
		return ""
	if not same_float(a.v, b.v):
		return "%s: %s vs %s" % [label, hex(a.v), hex(b.v)]
	return ""


## GdCN vs CN.
static func cn_diff(a, b, label: String) -> String:
	if a == null or b == null:
		return "" if a == null and b == null else "%s: %s vs %s" % [label, a, b]
	var r := num_diff(a.re, b.re, label + ".re")
	if r != "":
		return r
	return num_diff(a.im, b.im, label + ".im")


## GdHPBeam vs HPBeam.
static func beam_diff(a, b, label: String) -> String:
	if a == null or b == null:
		return "" if a == null and b == null else "%s: %s vs %s" % [label, a, b]
	var r := cn_diff(a.p, b.p, label + ".p")
	if r != "":
		return r
	r = cn_diff(a.tau, b.tau, label + ".tau")
	if r != "":
		return r
	return num_diff(a.kappa, b.kappa, label + ".kappa")


const EVENT_FIELDS := ["kind", "surface_id", "left", "applied", "terminal", "on_segment", "rehit",
	"popped", "teleport", "projective", "epoch", "seeded", "state", "anchored"]


## GdBeamSolver.Event vs BeamSolverEvent.
static func event_diff(a, b, label: String) -> String:
	for f in EVENT_FIELDS:
		if a.get(f) != b.get(f):
			return "%s.%s: %s vs %s" % [label, f, a.get(f), b.get(f)]
	if not same_float(a.margin, b.margin):
		return "%s.margin: %s vs %s" % [label, hex(a.margin), hex(b.margin)]
	if a.flags.keys() != b.flags.keys() or a.flags != b.flags:
		return "%s.flags: %s vs %s" % [label, a.flags, b.flags]
	var r := cn_diff(a.point, b.point, label + ".point")
	if r != "":
		return r
	r = num_diff(a.t_from, b.t_from, label + ".t_from")
	if r != "":
		return r
	r = num_diff(a.t_to, b.t_to, label + ".t_to")
	if r != "":
		return r
	return beam_diff(a.beam, b.beam, label + ".beam")


## Events of one walk. Beam identity (events sharing one beam object in
## GDScript, one beam_view natively) must partition the events the same way.
static func events_diff(a: Array, b: Array, label: String) -> String:
	if a.size() != b.size():
		return "%s: %d vs %d events (%s | %s)" % [label, a.size(), b.size(), describe(a), describe(b)]
	for i in a.size():
		var r := event_diff(a[i], b[i], "%s[%d]" % [label, i])
		if r != "":
			return r
	var pa := partition(a, func(e): return -1 if e.beam == null else e.beam.get_instance_id())
	var pb := partition(b, func(e): return e.beam_view)
	if pa != pb:
		return "%s: beam identity %s vs %s" % [label, pa, pb]
	return ""


## For each event the index of the first event with the same key.
static func partition(events: Array, key: Callable) -> Array:
	var first := {}
	var out: Array = []
	for i in events.size():
		var k = key.call(events[i])
		if not first.has(k):
			first[k] = i
		out.append(first[k])
	return out


static func describe(events: Array) -> String:
	return ", ".join(events.map(func(e): return e.describe()))


## Two float arrays (gains), bit for bit.
static func floats_diff(a: Array, b: Array, label: String) -> String:
	if a.size() != b.size():
		return "%s: %d vs %d values" % [label, a.size(), b.size()]
	for i in a.size():
		if not same_float(float(a[i]), float(b[i])):
			return "%s[%d]: %s vs %s" % [label, i, hex(a[i]), hex(b[i])]
	return ""


# ---------------------------------------------------------------- trace data (Stage 96)

static func bits32(v: Vector2) -> PackedByteArray:
	return PackedFloat32Array([v.x, v.y]).to_byte_array()


static func same_vec(a: Vector2, b: Vector2) -> bool:
	return bits32(a) == bits32(b)


## GdTraceStep vs TraceStep. The native hit carries the surface id where the
## GDScript hit carries the surface's Segment.
static func step_diff(a, b, label: String) -> String:
	for f in ["start", "end", "via"]:
		if not same_vec(a.get(f), b.get(f)):
			return "%s.%s: %s vs %s" % [label, f, a.get(f), b.get(f)]
	for f in ["frame_id", "type", "surface_id", "hit_side", "hit_on_segment", "after_portal", "ghost", "is_arc_step", "precision_bits"]:
		if a.get(f) != b.get(f):
			return "%s.%s: %s vs %s" % [label, f, a.get(f), b.get(f)]
	if not same_float(a.est_error, b.est_error):
		return "%s.est_error: %s vs %s" % [label, hex(a.est_error), hex(b.est_error)]
	if (a.hit == null) != (b.hit == null):
		return "%s.hit: %s vs %s" % [label, a.hit, b.hit]
	if a.hit != null:
		if not same_float(a.hit.t, b.hit.t):
			return "%s.hit.t: %s vs %s" % [label, hex(a.hit.t), hex(b.hit.t)]
		if not same_vec(a.hit.point.coords, b.hit.point.coords):
			return "%s.hit.point: %s vs %s" % [label, a.hit.point.coords, b.hit.point]
		var sid: int = -1 if a.hit.segment == null else a.surface_id
		if sid != b.hit.surface_id:
			return "%s.hit.surface: %d vs %d" % [label, sid, b.hit.surface_id]
		if int(a.hit.side) != b.hit.side or a.hit.on_segment != b.hit.on_segment:
			return "%s.hit.side/on_segment: (%s, %s) vs (%s, %s)" % [label, a.hit.side, a.hit.on_segment, b.hit.side, b.hit.on_segment]
	if (a.ray == null) != (b.ray == null):
		return "%s.ray: %s vs %s" % [label, a.ray, b.ray]
	if a.ray != null:
		if not same_vec(a.ray.origin.coords, b.ray.origin.coords):
			return "%s.ray.origin: %s vs %s" % [label, a.ray.origin.coords, b.ray.origin]
		if not same_vec(a.ray.direction.start.coords, b.ray.direction.start.coords) or not same_vec(a.ray.direction.end.coords, b.ray.direction.end.coords):
			return "%s.ray.direction differs" % label
	return ""


## GdTracePath vs TracePath.
static func path_diff(a, b, label: String) -> String:
	if a == null or b == null:
		return "" if a == null and b == null else "%s: %s vs %s" % [label, a, b]
	if a.steps.size() != b.steps.size():
		return "%s: %d vs %d steps" % [label, a.steps.size(), b.steps.size()]
	for i in a.steps.size():
		var r := step_diff(a.steps[i], b.steps[i], "%s.steps[%d]" % [label, i])
		if r != "":
			return r
	var ta: Array = a.targets_hit.keys()
	var tb: Array = b.targets_hit.keys()
	ta.sort()
	tb.sort()
	if ta != tb:
		return "%s.targets_hit: %s vs %s" % [label, ta, tb]
	if a.cursor_index != b.cursor_index or a.hit_count != b.hit_count:
		return "%s: cursor_index/hit_count (%d, %d) vs (%d, %d)" % [label, a.cursor_index, a.hit_count, b.cursor_index, b.hit_count]
	return ""


## GdErrorEstimate.Sensitivity vs ErrorEstimateSensitivity. The native
## sensitivity of a ladder result keeps its float64 events only when tier 0
## was accepted, so events compare only when the native side has them.
static func sens_diff(a, b, label: String) -> String:
	if a == null or b == null:
		return "" if a == null and b == null else "%s: %s vs %s" % [label, a, b]
	var r := floats_diff(a.gain, b.gain, label + ".gain")
	if r != "":
		return r
	if not same_float(a.max_gain, b.max_gain):
		return "%s.max_gain: %s vs %s" % [label, hex(a.max_gain), hex(b.max_gain)]
	if a.unreliable_from != b.unreliable_from:
		return "%s.unreliable_from: %d vs %d" % [label, a.unreliable_from, b.unreliable_from]
	if b.events.size() > 0:
		return events_diff(a.events, b.events, label + ".events")
	return ""


## GdPrecisionLadder.Result vs PrecisionLadderResult (the usec fields are timing).
static func result_diff(a, b, label: String) -> String:
	if a == null or b == null:
		return "" if a == null and b == null else "%s: %s vs %s" % [label, a, b]
	for f in ["planned_tier", "physical_tier", "converged", "tiers_tried", "planned_from_physical"]:
		if a.get(f) != b.get(f):
			return "%s.%s: %s vs %s" % [label, f, a.get(f), b.get(f)]
	var r := floats_diff(a.planned_err, b.planned_err, label + ".planned_err")
	if r == "":
		r = floats_diff(a.physical_err, b.physical_err, label + ".physical_err")
	if r == "":
		r = events_diff(a.planned, b.planned, label + ".planned")
	if r == "":
		r = events_diff(a.physical, b.physical, label + ".physical")
	if r == "":
		r = sens_diff(a.physical_sens, b.physical_sens, label + ".physical_sens")
	return r


## GdBeamTracer.Pair vs BeamTracerPair.
static func pair_diff(a, b, label: String) -> String:
	if a == null or b == null:
		return "" if a == null and b == null else "%s: %s vs %s" % [label, a, b]
	var r := path_diff(a.planned, b.planned, label + ".planned")
	if r == "":
		r = path_diff(a.physical, b.physical, label + ".physical")
	if r == "":
		r = result_diff(a.result, b.result, label + ".result")
	if r == "" and (a.final_flags != b.final_flags or a.final_flags.keys() != b.final_flags.keys()):
		r = "%s.final_flags: %s vs %s" % [label, a.final_flags, b.final_flags]
	return r
