class_name Exact
extends RefCounted

# Exact geometric predicates on float64 data (robust-geometry style): a
# float64 evaluation with a rigorous forward error bound decides whenever the
# value is certainly non-zero; otherwise the value is evaluated exactly with
# BigFloat at adaptive width (no truncation: products get the limbs of both
# factors, sums the limbs spanning both exponents). The answer is always the
# exact one; the bound only selects the cheap path, it never decides a result.

const U := 1.1102230246251565e-16          # 2^-53, unit roundoff

# ---------------------------------------------------------------- exact ops

static func of(x: float) -> BigFloat:
	return BigFloat.from_float(x, 2)       # 53 bits fit in 2 limbs (56 bits)

static func mul(a: BigFloat, b: BigFloat) -> BigFloat:
	if a.is_zero() or b.is_zero():
		return BigFloat.zero(2)
	return a.with_limbs(a.limbs.size() + b.limbs.size()).mul(b)

static func add(a: BigFloat, b: BigFloat) -> BigFloat:
	if a.is_zero():
		return b
	if b.is_zero():
		return a
	var gap: int = absi(a.top_exp() - b.top_exp())
	var n: int = maxi(a.limbs.size(), b.limbs.size()) + gap / BigFloat.BITS + 3
	return a.with_limbs(n).add(b.with_limbs(n))

static func sub(a: BigFloat, b: BigFloat) -> BigFloat:
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
# A line in implicit form a x + b y = c with exact (BigFloat) coefficients, as
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
	var w: BigFloat = X[2]
	# conj(X - s w)
	var cx := sub(X[0], mul(of(sx), w))
	var cy := sub(mul(of(sy), w), X[1])
	var swm := mul(w, mm)
	var rx := add(mul(of(sx), swm), sub(mul(m2r, cx), mul(m2i, cy)))
	var ry := add(mul(of(sy), swm), add(mul(m2r, cy), mul(m2i, cx)))
	return [rx, ry, swm]

# The line through (px, py) and the homogeneous point X.
static func line_through_h(px: float, py: float, X: Array) -> Array:
	var w: BigFloat = X[2]
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

# Exact: the homogeneous point X lies on the line L (a x + b y = c w).
static func on_line_h(L: Array, X: Array) -> bool:
	return sub(add(mul(L[0], X[0]), mul(L[1], X[1])), mul(L[2], X[2])).is_zero()

# Exact: the line L is tangent to the circle with homogeneous center C and
# radius² times w² rw2: (a cx + b cy - c w)² == rw2 (a² + b²).
static func tangent_zero(L: Array, C: Array, rw2: BigFloat) -> bool:
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
	var a: BigFloat = L[0]
	var b: BigFloat = L[1]
	var c: BigFloat = L[2]
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
	var _exact = null  # [a, b, c] BigFloat, or null until needed
	var parent: Line = null
	var mirror: PackedFloat64Array

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

	func exact() -> Array:
		if _exact == null:
			var L: Array = parent.exact()
			if mirror.size() == 2:
				# translated by (dx, dy): c' = c + a dx + b dy
				_exact = [L[0], L[1], Exact.add(L[2], Exact.add(Exact.mul(L[0], Exact.of(mirror[0])), Exact.mul(L[1], Exact.of(mirror[1]))))]
			else:
				_exact = Exact.reflect_line(L, mirror[0], mirror[1], mirror[2], mirror[3])
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
		var c2 := Exact._eadd([fc, ec], Exact._eadd(Exact._emul([fa, ea], [dx, 0.0]), Exact._emul([fb, eb], [dy, 0.0])))
		o.fc = c2[0]
		o.ec = c2[1]
		return o

	# The reflection in the mirror line s -> e (float64 part with its bound).
	func reflected(sx: float, sy: float, ex: float, ey: float) -> Line:
		var o := Line.new()
		o.parent = self
		o.mirror = PackedFloat64Array([sx, sy, ex, ey])
		var mx := Exact._esub([ex, 0.0], [sx, 0.0])
		var my := Exact._esub([ey, 0.0], [sy, 0.0])
		var mm := Exact._eadd(Exact._emul(mx, mx), Exact._emul(my, my))
		var m2r := Exact._esub(Exact._emul(mx, mx), Exact._emul(my, my))
		var m2i := Exact._eadd(Exact._emul(mx, my), Exact._emul(my, mx))
		var a := [fa, ea]
		var b := [fb, eb]
		var c := [fc, ec]
		var esx := [sx, 0.0]
		var esy := [sy, 0.0]
		var a2 := Exact._eadd(Exact._emul(a, m2r), Exact._emul(b, m2i))
		var b2 := Exact._esub(Exact._emul(a, m2i), Exact._emul(b, m2r))
		var c2 := Exact._eadd(Exact._esub(Exact._emul(c, mm), Exact._emul(Exact._eadd(Exact._emul(a, esx), Exact._emul(b, esy)), mm)),
			Exact._eadd(Exact._emul(a2, esx), Exact._emul(b2, esy)))
		o.fa = a2[0]
		o.ea = a2[1]
		o.fb = b2[0]
		o.eb = b2[1]
		o.fc = c2[0]
		o.ec = c2[1]
		return o

	# Exact: (x, y) on the line (exact float data point).
	func contains(x: float, y: float) -> bool:
		var v := Exact._eadd(Exact._esub(Exact._emul([fa, ea], [x, 0.0]), [fc, ec]), Exact._emul([fb, eb], [y, 0.0]))
		if absf(v[0]) > v[1]:
			return false
		var L := exact()
		return Exact.sub(Exact.add(Exact.mul(L[0], Exact.of(x)), Exact.mul(L[1], Exact.of(y))), L[2]).is_zero()

	# Exact: the homogeneous exact point X (float roundings x, y with bounds
	# ex, ey of X / w) on the line.
	func contains_h(X: Array, x: float, y: float, exy: float) -> bool:
		var v := Exact._eadd(Exact._esub(Exact._emul([fa, ea], [x, exy]), [fc, ec]), Exact._emul([fb, eb], [y, exy]))
		if absf(v[0]) > v[1]:
			return false
		return Exact.on_line_h(exact(), X)

	# Exact: tangent to the circle with exact homogeneous center C and rw2
	# (float roundings cx, cy, rr with bounds ecc, err).
	func tangent_to(C: Array, rw2: BigFloat, cx: float, cy: float, rr: float, ecc: float, err: float) -> bool:
		var d := Exact._eadd(Exact._esub(Exact._emul([fa, ea], [cx, ecc]), [fc, ec]), Exact._emul([fb, eb], [cy, ecc]))
		var n2 := Exact._eadd(Exact._emul([fa, ea], [fa, ea]), Exact._emul([fb, eb], [fb, eb]))
		var v := Exact._esub(Exact._emul(d, d), Exact._emul([rr, err], n2))
		if absf(v[0]) > v[1]:
			return false
		return Exact.tangent_zero(exact(), C, rw2)

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
