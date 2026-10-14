class_name GdExact
extends RefCounted

# GdExact geometric predicates on float64 data (robust-geometry style): a
# float64 evaluation with a rigorous forward error bound decides whenever the
# value is certainly non-zero; otherwise the value is evaluated exactly with
# GdBigFloat at adaptive width (no truncation: products get the limbs of both
# factors, sums the limbs spanning both exponents). The answer is always the
# exact one; the bound only selects the cheap path, it never decides a result.

const U := 1.1102230246251565e-16          # 2^-53, unit roundoff

# ---------------------------------------------------------------- exact ops

static func of(x: float) -> GdBigFloat:
	return GdBigFloat.from_float(x, 2)       # 53 bits fit in 2 limbs (56 bits)

static func mul(a: GdBigFloat, b: GdBigFloat) -> GdBigFloat:
	if a.is_zero() or b.is_zero():
		return GdBigFloat.zero(2)
	return a.with_limbs(a.limbs.size() + b.limbs.size()).mul(b)

static func add(a: GdBigFloat, b: GdBigFloat) -> GdBigFloat:
	if a.is_zero():
		return b
	if b.is_zero():
		return a
	var gap: int = absi(a.top_exp() - b.top_exp())
	var n: int = maxi(a.limbs.size(), b.limbs.size()) + gap / GdBigFloat.BITS + 3
	return a.with_limbs(n).add(b.with_limbs(n))

static func sub(a: GdBigFloat, b: GdBigFloat) -> GdBigFloat:
	return add(a, b.neg())

# ---------------------------------------------------------------- predicates

# Sign-exact test: cross(b - a, c - a) == 0 (c on the line through a and b).
static func orient_zero(ax: float, ay: float, bx: float, by: float, cx: float, cy: float) -> bool:
	var l := (bx - ax) * (cy - ay)
	var r := (by - ay) * (cx - ax)
	var det := l - r
	if absf(det) > 4.0 * U * (absf(l) + absf(r)):
		return false
	var e := sub(mul(sub(of(bx), of(ax)), sub(of(cy), of(ay))), mul(sub(of(by), of(ay)), sub(of(cx), of(ax))))
	return e.is_zero()

# Sign-exact test: |k - c|² == r² (full circle with exact center and radius).
static func on_circle_zero(cx: float, cy: float, r: float, kx: float, ky: float) -> bool:
	var dx := kx - cx
	var dy := ky - cy
	var a := dx * dx + dy * dy
	var rr := r * r
	if absf(a - rr) > 8.0 * U * (a + rr):
		return false
	var ex := sub(of(kx), of(cx))
	var ey := sub(of(ky), of(cy))
	var e := sub(add(mul(ex, ex), mul(ey, ey)), mul(of(r), of(r)))
	return e.is_zero()

# Sign-exact test: d on the circle through a, b, c (incircle determinant == 0).
static func incircle_zero(ax: float, ay: float, bx: float, by: float, cx: float, cy: float, dx: float, dy: float) -> bool:
	var adx := ax - dx
	var ady := ay - dy
	var bdx := bx - dx
	var bdy := by - dy
	var cdx := cx - dx
	var cdy := cy - dy
	var alift := adx * adx + ady * ady
	var blift := bdx * bdx + bdy * bdy
	var clift := cdx * cdx + cdy * cdy
	var bc := bdx * cdy - cdx * bdy
	var ca := cdx * ady - adx * cdy
	var ab := adx * bdy - bdx * ady
	var det := alift * bc + blift * ca + clift * ab
	var perm := (absf(bdx * cdy) + absf(cdx * bdy)) * alift + (absf(cdx * ady) + absf(adx * cdy)) * blift \
		+ (absf(adx * bdy) + absf(bdx * ady)) * clift
	if absf(det) > 16.0 * U * perm:
		return false
	var eax := sub(of(ax), of(dx))
	var eay := sub(of(ay), of(dy))
	var ebx := sub(of(bx), of(dx))
	var eby := sub(of(by), of(dy))
	var ecx := sub(of(cx), of(dx))
	var ecy := sub(of(cy), of(dy))
	var ea := add(mul(eax, eax), mul(eay, eay))
	var eb := add(mul(ebx, ebx), mul(eby, eby))
	var ec := add(mul(ecx, ecx), mul(ecy, ecy))
	var ebc := sub(mul(ebx, ecy), mul(ecx, eby))
	var eca := sub(mul(ecx, eay), mul(eax, ecy))
	var eab := sub(mul(eax, eby), mul(ebx, eay))
	return add(add(mul(ea, ebc), mul(eb, eca)), mul(ec, eab)).is_zero()

# ---------------------------------------------------------------- exact lines
# A line in implicit form a x + b y = c with exact (GdBigFloat) coefficients, as
# an Array [a, b, c] (any nonzero scale).

static func line_from_dir(px: float, py: float, dx: float, dy: float) -> Array:
	var a := of(dy).neg()
	var b := of(dx)
	return [a, b, add(mul(a, of(px)), mul(b, of(py)))]

static func line_through(px: float, py: float, qx: float, qy: float) -> Array:
	var a := sub(of(qy), of(py)).neg()
	var b := sub(of(qx), of(px))
	return [a, b, add(mul(a, of(px)), mul(b, of(py)))]

# Homogeneous exact point [x, y, w] (the point (x/w, y/w), w > 0) reflected in
# the mirror line s -> e: R(z) = s + M² conj(z - s) / |M|², times w |M|².
static func reflect_point_h(X: Array, sx: float, sy: float, ex: float, ey: float) -> Array:
	var mx := sub(of(ex), of(sx))
	var my := sub(of(ey), of(sy))
	var mm := add(mul(mx, mx), mul(my, my))
	var m2r := sub(mul(mx, mx), mul(my, my))
	var m2i := add(mul(mx, my), mul(my, mx))
	var w: GdBigFloat = X[2]
	# conj(X - s w)
	var cx := sub(X[0], mul(of(sx), w))
	var cy := sub(mul(of(sy), w), X[1])
	var swm := mul(w, mm)
	var rx := add(mul(of(sx), swm), sub(mul(m2r, cx), mul(m2i, cy)))
	var ry := add(mul(of(sy), swm), add(mul(m2r, cy), mul(m2i, cx)))
	return [rx, ry, swm]

# Homogeneous X inverted in the circle with homogeneous center C and radius²
# times C_w² rw2: X' = c + r² (X - c) / |X - c|², over the common denominator
# C_w |X w C_w - C w|² (made positive). null at the center (image at infinity).
static func invert_point_h(X: Array, C: Array, rw2: GdBigFloat):
	var w: GdBigFloat = X[2]
	var cw: GdBigFloat = C[2]
	var dx := sub(mul(X[0], cw), mul(C[0], w))
	var dy := sub(mul(X[1], cw), mul(C[1], w))
	var dd := add(mul(dx, dx), mul(dy, dy))
	if dd.is_zero():
		return null
	var rw := mul(rw2, w)
	var out := [add(mul(C[0], dd), mul(rw, dx)), add(mul(C[1], dd), mul(rw, dy)), mul(cw, dd)]
	if out[2].is_negative():
		out = [out[0].neg(), out[1].neg(), out[2].neg()]
	return out

# The line through (px, py) and the homogeneous point X.
static func line_through_h(px: float, py: float, X: Array) -> Array:
	var w: GdBigFloat = X[2]
	var a := sub(X[1], mul(of(py), w)).neg()
	var b := sub(X[0], mul(of(px), w))
	return [a, b, add(mul(a, of(px)), mul(b, of(py)))]

# Circumcenter of a, b, c as a homogeneous exact point [x, y, w].
static func circumcenter_h(ax: float, ay: float, bx: float, by: float, cx: float, cy: float) -> Array:
	var eax := of(ax)
	var eay := of(ay)
	var ebx := of(bx)
	var eby := of(by)
	var ecx := of(cx)
	var ecy := of(cy)
	var la := add(mul(eax, eax), mul(eay, eay))
	var lb := add(mul(ebx, ebx), mul(eby, eby))
	var lc := add(mul(ecx, ecx), mul(ecy, ecy))
	var byc := sub(eby, ecy)
	var cya := sub(ecy, eay)
	var ayb := sub(eay, eby)
	var w := mul(of(2.0), add(add(mul(eax, byc), mul(ebx, cya)), mul(ecx, ayb)))
	var nx := add(add(mul(la, byc), mul(lb, cya)), mul(lc, ayb))
	var ny := add(add(mul(la, sub(ecx, ebx)), mul(lb, sub(eax, ecx))), mul(lc, sub(ebx, eax)))
	return [nx, ny, w]

# GdExact: the homogeneous point X lies on the line L (a x + b y = c w).
static func on_line_h(L: Array, X: Array) -> bool:
	return sub(add(mul(L[0], X[0]), mul(L[1], X[1])), mul(L[2], X[2])).is_zero()

# GdExact: the line L is tangent to the circle with homogeneous center C and
# radius² times w² rw2: (a cx + b cy - c w)² == rw2 (a² + b²).
static func tangent_zero(L: Array, C: Array, rw2: GdBigFloat) -> bool:
	var d := sub(add(mul(L[0], C[0]), mul(L[1], C[1])), mul(L[2], C[2]))
	var n2 := add(mul(L[0], L[0]), mul(L[1], L[1]))
	return sub(mul(d, d), mul(rw2, n2)).is_zero()

# Float filter for tangent_zero: false when certainly not tangent. cx, cy, rr
# are float64 roundings (a few ulps) of the exact center and radius².
static func maybe_tangent(L: Array, cx: float, cy: float, rr: float) -> bool:
	var fa: float = L[0].to_float()
	var fb: float = L[1].to_float()
	var fc: float = L[2].to_float()
	var d := fa * cx + fb * cy - fc
	var n2 := fa * fa + fb * fb
	var v := d * d - rr * n2
	var mag := absf(fa * cx) + absf(fb * cy) + absf(fc)
	return absf(v) <= 64.0 * U * (mag * mag + absf(rr) * n2)

# Sign-exact test: (x, y) on the line L.
static func on_line_zero(L: Array, x: float, y: float) -> bool:
	var fa: float = L[0].to_float()
	var fb: float = L[1].to_float()
	var fc: float = L[2].to_float()
	var l := fa * x
	var r := fb * y
	var v := l + r - fc
	if absf(v) > 8.0 * U * (absf(l) + absf(r) + absf(fc)):
		return false
	return sub(add(mul(L[0], of(x)), mul(L[1], of(y))), L[2]).is_zero()

# The reflection of the line L in the mirror line through s and e, exactly:
# z on L' iff R(z) on L, R(z) = s + M² conj(z - s) / |M|², M = e - s;
# multiplied through by |M|² (polynomial in the data, no division).
static func reflect_line(L: Array, sx: float, sy: float, ex: float, ey: float) -> Array:
	var mx := sub(of(ex), of(sx))
	var my := sub(of(ey), of(sy))
	var mm := add(mul(mx, mx), mul(my, my))
	var m2r := sub(mul(mx, mx), mul(my, my))
	var m2i := add(mul(mx, my), mul(my, mx))
	var a: GdBigFloat = L[0]
	var b: GdBigFloat = L[1]
	var c: GdBigFloat = L[2]
	var esx := of(sx)
	var esy := of(sy)
	var a2 := add(mul(a, m2r), mul(b, m2i))
	var b2 := sub(mul(a, m2i), mul(b, m2r))
	# c' = c|M|² - (a Sx + b Sy)|M|² + a' Sx + b' Sy
	var c2 := add(sub(mul(c, mm), mul(add(mul(a, esx), mul(b, esy)), mm)), add(mul(a2, esx), mul(b2, esy)))
	return [a2, b2, c2]

# ---------------------------------------------------------------- lazy exact lines

# An exact line a x + b y = c, carried lazily: float64 coefficients with a
# rigorous forward error bound (running error analysis, standard rounding
# model), and the exact coefficients computed only when a predicate's float
# filter cannot decide (a genuine near-coincidence). A reflected line keeps
# its parent and mirror; its exact coefficients are derived on demand.
class Line extends RefCounted:
	var fa: float
	var fb: float
	var fc: float
	var ea: float      # |fa - a| <= ea, ...
	var eb: float
	var ec: float
	var _exact = null  # [a, b, c] GdBigFloat, or null until needed
	var parent: Line = null
	var mirror: PackedFloat64Array
	var source: Callable   # computes the exact coefficients on demand (from_approx)

	static func from_exact(L: Array) -> Line:
		var o := Line.new()
		o._exact = L
		o.fa = L[0].to_float()
		o.fb = L[1].to_float()
		o.fc = L[2].to_float()
		o.ea = 2.0 * U * absf(o.fa)
		o.eb = 2.0 * U * absf(o.fb)
		o.ec = 2.0 * U * absf(o.fc)
		return o

	# A line known by float64 coefficients with rigorous bounds ([value,
	# bound] pairs); `source` computes the exact coefficients when a predicate
	# cannot decide from the bounds.
	static func from_approx(a: Array, b: Array, c: Array, p_source: Callable) -> Line:
		var o := Line.new()
		o.fa = a[0]
		o.ea = a[1]
		o.fb = b[0]
		o.eb = b[1]
		o.fc = c[0]
		o.ec = c[1]
		o.source = p_source
		return o

	# The exact coefficients; null when a lazy source could not afford them
	# (the line then makes no exact claim: its predicates answer false).
	var _source_done := false
	func exact():
		if _exact == null and source.is_valid():
			if not _source_done:
				_source_done = true
				_exact = source.call()
			return _exact
		if _exact == null:
			var L = parent.exact()
			if L == null:
				return null
			if mirror.size() == 2:
				# translated by (dx, dy): c' = c + a dx + b dy
				_exact = [L[0], L[1], GdExact.add(L[2], GdExact.add(GdExact.mul(L[0], GdExact.of(mirror[0])), GdExact.mul(L[1], GdExact.of(mirror[1]))))]
			else:
				_exact = GdExact.reflect_line(L, mirror[0], mirror[1], mirror[2], mirror[3])
		return _exact

	# The translation by (dx, dy) (float64 part with its bound).
	func translated(dx: float, dy: float) -> Line:
		var o := Line.new()
		o.parent = self
		o.mirror = PackedFloat64Array([dx, dy])
		o.fa = fa
		o.ea = ea
		o.fb = fb
		o.eb = eb
		var c2 := GdExact._eadd([fc, ec], GdExact._eadd(GdExact._emul([fa, ea], [dx, 0.0]), GdExact._emul([fb, eb], [dy, 0.0])))
		o.fc = c2[0]
		o.ec = c2[1]
		return o

	# The reflection in the mirror line s -> e (float64 part with its bound).
	func reflected(sx: float, sy: float, ex: float, ey: float) -> Line:
		var o := Line.new()
		o.parent = self
		o.mirror = PackedFloat64Array([sx, sy, ex, ey])
		var mx := GdExact._esub([ex, 0.0], [sx, 0.0])
		var my := GdExact._esub([ey, 0.0], [sy, 0.0])
		var mm := GdExact._eadd(GdExact._emul(mx, mx), GdExact._emul(my, my))
		var m2r := GdExact._esub(GdExact._emul(mx, mx), GdExact._emul(my, my))
		var m2i := GdExact._eadd(GdExact._emul(mx, my), GdExact._emul(my, mx))
		var a := [fa, ea]
		var b := [fb, eb]
		var c := [fc, ec]
		var esx := [sx, 0.0]
		var esy := [sy, 0.0]
		var a2 := GdExact._eadd(GdExact._emul(a, m2r), GdExact._emul(b, m2i))
		var b2 := GdExact._esub(GdExact._emul(a, m2i), GdExact._emul(b, m2r))
		var c2 := GdExact._eadd(GdExact._esub(GdExact._emul(c, mm), GdExact._emul(GdExact._eadd(GdExact._emul(a, esx), GdExact._emul(b, esy)), mm)),
			GdExact._eadd(GdExact._emul(a2, esx), GdExact._emul(b2, esy)))
		o.fa = a2[0]
		o.ea = a2[1]
		o.fb = b2[0]
		o.eb = b2[1]
		o.fc = c2[0]
		o.ec = c2[1]
		return o

	# GdExact: this line and the carriers of segments p -> q and r -> s meet at
	# one point (det [L; A; B] == 0).
	func concurrent(px: float, py: float, qx: float, qy: float, rx: float, ry: float, sx: float, sy: float) -> bool:
		# float filter (scalar; first-order rigorous bound with generous
		# constants: every intermediate carries at most a few ulps relative)
		var a1 := py - qy
		var b1 := qx - px
		var c1 := a1 * px + b1 * py
		var a2 := ry - sy
		var b2 := sx - rx
		var c2 := a2 * rx + b2 * ry
		var c1m := absf(a1 * px) + absf(b1 * py)
		var c2m := absf(a2 * rx) + absf(b2 * ry)
		var m1 := b1 * c2 - b2 * c1
		var m2 := a1 * c2 - a2 * c1
		var m3 := a1 * b2 - a2 * b1
		var m1m := absf(b1) * c2m + absf(b2) * c1m
		var m2m := absf(a1) * c2m + absf(a2) * c1m
		var m3m := absf(a1 * b2) + absf(a2 * b1)
		var det := fa * m1 - fb * m2 + fc * m3
		var bound := 32.0 * U * (absf(fa) * m1m + absf(fb) * m2m + absf(fc) * m3m) \
			+ ea * m1m + eb * m2m + ec * m3m
		if absf(det) > bound:
			return false
		var L = exact()
		if L == null:
			return false
		var A := GdExact.line_through(px, py, qx, qy)
		var B := GdExact.line_through(rx, ry, sx, sy)
		var e1 := GdExact.sub(GdExact.mul(A[1], B[2]), GdExact.mul(B[1], A[2]))
		var e2 := GdExact.sub(GdExact.mul(A[0], B[2]), GdExact.mul(B[0], A[2]))
		var e3 := GdExact.sub(GdExact.mul(A[0], B[1]), GdExact.mul(B[0], A[1]))
		return GdExact.add(GdExact.sub(GdExact.mul(L[0], e1), GdExact.mul(L[1], e2)), GdExact.mul(L[2], e3)).is_zero()

	# GdExact: (x, y) on the line (exact float data point).
	func contains(x: float, y: float) -> bool:
		var l := fa * x
		var r := fb * y
		var v := l + r - fc
		if absf(v) > 4.0 * U * (absf(l) + absf(r) + absf(fc)) + ea * absf(x) + eb * absf(y) + ec:
			return false
		var L = exact()
		if L == null:
			return false
		return GdExact.sub(GdExact.add(GdExact.mul(L[0], GdExact.of(x)), GdExact.mul(L[1], GdExact.of(y))), L[2]).is_zero()

	# GdExact: the homogeneous exact point X (float roundings x, y with bounds
	# ex, ey of X / w) on the line.
	func contains_h(X: Array, x: float, y: float, exy: float) -> bool:
		var v := GdExact._eadd(GdExact._esub(GdExact._emul([fa, ea], [x, exy]), [fc, ec]), GdExact._emul([fb, eb], [y, exy]))
		if absf(v[0]) > v[1]:
			return false
		var L = exact()
		return L != null and GdExact.on_line_h(L, X)

	# GdExact: tangent to the circle with exact homogeneous center C and rw2
	# (float roundings cx, cy, rr with bounds ecc, err).
	func tangent_to(C: Array, rw2: GdBigFloat, cx: float, cy: float, rr: float, ecc: float, err: float) -> bool:
		var d := GdExact._eadd(GdExact._esub(GdExact._emul([fa, ea], [cx, ecc]), [fc, ec]), GdExact._emul([fb, eb], [cy, ecc]))
		var n2 := GdExact._eadd(GdExact._emul([fa, ea], [fa, ea]), GdExact._emul([fb, eb], [fb, eb]))
		var v := GdExact._esub(GdExact._emul(d, d), GdExact._emul([rr, err], n2))
		if absf(v[0]) > v[1]:
			return false
		var L = exact()
		return L != null and GdExact.tangent_zero(L, C, rw2)

# Running error analysis on [value, bound] pairs (standard model, u = 2^-53).
static func _emul(x: Array, y: Array) -> Array:
	var v: float = x[0] * y[0]
	return [v, absf(x[0]) * y[1] + absf(y[0]) * x[1] + x[1] * y[1] + U * absf(v)]

static func _eadd(x: Array, y: Array) -> Array:
	var v: float = x[0] + y[0]
	return [v, x[1] + y[1] + U * absf(v)]

static func _esub(x: Array, y: Array) -> Array:
	var v: float = x[0] - y[0]
	return [v, x[1] + y[1] + U * absf(v)]
