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


## GDScript scalar (F64Num / DualNum / BigFloat / null) vs NNum (or null).
static func num_diff(a, b, label: String) -> String:
	if a == null or b == null:
		return "" if a == null and b == null else "%s: %s vs %s" % [label, a, b]
	if a is BigFloat:
		if a.sign != b.sign or a.exp != b.exp or a.limbs != b.get_limbs():
			return "%s: bigfloat (%d, %d, %s) vs (%d, %d, %s)" % [label, a.sign, a.exp, a.limbs, b.sign, b.exp, b.get_limbs()]
		return ""
	if a is DualNum:
		if not same_float(a.v, b.v) or not same_float(a.d, b.d):
			return "%s: dual (%s, %s) vs (%s, %s)" % [label, hex(a.v), hex(a.d), hex(b.v), hex(b.d)]
		return ""
	if not same_float(a.v, b.v):
		return "%s: %s vs %s" % [label, hex(a.v), hex(b.v)]
	return ""


## CN vs NCN.
static func cn_diff(a, b, label: String) -> String:
	if a == null or b == null:
		return "" if a == null and b == null else "%s: %s vs %s" % [label, a, b]
	var r := num_diff(a.re, b.re, label + ".re")
	if r != "":
		return r
	return num_diff(a.im, b.im, label + ".im")


## HPBeam vs NHPBeam.
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


## BeamSolver.Event vs NBeamSolverEvent.
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
