class_name GdBigFloat
extends RefCounted

# Arbitrary-precision binary floating point.
# value = sign * M * 2^exp, M = sum(limbs[i] * 2^(BITS*i)), limbs little-endian,
# normalized so the top limb has bit (BITS-1) set. Precision = BITS * limbs.size().
# Rounding is toward zero (truncation): every op is accurate to ~1 ulp.

const BITS := 28
const BASE := 1 << 28
const MASK := (1 << 28) - 1
const TOP_BIT := 1 << 27

var sign: int = 0
var exp: int = 0
var limbs: PackedInt64Array

static func zero(n: int) -> GdBigFloat:
	var z := GdBigFloat.new()
	z.limbs = PackedInt64Array()
	z.limbs.resize(n)
	return z

# +infinity (the projective parameter's t = inf): a reserved exponent, so
# comparisons order it above every finite value and to_float() gives INF.
const INF_EXP := 1 << 40

static func infinity(n: int) -> GdBigFloat:
	var r := GdBigFloat.from_int(1, n)
	r.exp = INF_EXP
	return r

func is_inf() -> bool:
	return sign > 0 and exp >= INF_EXP

static func limbs_for_bits(bits: int) -> int:
	return maxi(2, (bits + BITS - 1) / BITS)

static func from_float(f: float, n: int) -> GdBigFloat:
	var r := GdBigFloat.zero(n)
	if f == 0.0:
		return r
	r.sign = 1 if f > 0.0 else -1
	var m: float = absf(f)
	# scale m into [2^52, 2^53): exact power-of-two scaling
	var e: int = int(floor(log(m) / log(2.0))) - 52
	m = m / pow(2.0, e) if e > -1000 else (m * pow(2.0, 600)) / pow(2.0, e + 600)
	while m >= 9007199254740992.0:
		m *= 0.5
		e += 1
	while m < 4503599627370496.0:
		m *= 2.0
		e -= 1
	var mi: int = int(m)  # 53-bit integer
	# place the 53-bit integer at the top of n limbs (bit 27 of top limb set)
	# 53 bits = top limb 28 bits + next 25 bits
	var top: int = mi >> 25
	var low: int = (mi & ((1 << 25) - 1)) << 3
	r.limbs[n - 1] = top
	r.limbs[n - 2] = low
	r.exp = e - 3 - BITS * (n - 2)
	return r

static func from_string(s: String, n: int) -> GdBigFloat:
	# decimal string "[-]digits[.digits][e[+-]digits]"
	var str := s.strip_edges()
	var neg := false
	if str.begins_with("-"):
		neg = true
		str = str.substr(1)
	elif str.begins_with("+"):
		str = str.substr(1)
	var e10 := 0
	var epos := str.findn("e")
	if epos >= 0:
		e10 = str.substr(epos + 1).to_int()
		str = str.substr(0, epos)
	var dot := str.find(".")
	if dot >= 0:
		e10 -= str.length() - dot - 1
		str = str.substr(0, dot) + str.substr(dot + 1)
	var wp := n + 1
	var acc := GdBigFloat.zero(wp)
	var ten := GdBigFloat.from_int(10, wp)
	# process 6 digits at a time
	var i := 0
	while i < str.length():
		var chunk_len := mini(6, str.length() - i)
		var chunk := str.substr(i, chunk_len).to_int()
		acc = acc.mul(GdBigFloat.from_int(int(pow(10, chunk_len)), wp)).add(GdBigFloat.from_int(chunk, wp))
		i += chunk_len
	if e10 != 0:
		var p := GdBigFloat.from_int(1, wp)
		var base := ten
		var k: int = absi(e10)
		while k > 0:
			if k & 1:
				p = p.mul(base)
			base = base.mul(base)
			k >>= 1
		acc = acc.mul(p) if e10 > 0 else acc.div(p)
	if neg:
		acc = acc.neg()
	return acc.with_limbs(n)

static func from_int(v: int, n: int) -> GdBigFloat:
	if v == 0:
		return GdBigFloat.zero(n)
	var a := PackedInt64Array()
	var x: int = absi(v)
	while x > 0:
		a.append(x & MASK)
		x >>= BITS
	return _normalize(a, 0, 1 if v > 0 else -1, n)

func precision_limbs() -> int:
	return limbs.size()

func is_zero() -> bool:
	return sign == 0

func is_negative() -> bool:
	return sign < 0

func neg() -> GdBigFloat:
	var r := GdBigFloat.new()
	r.sign = -sign
	r.exp = exp
	r.limbs = limbs
	return r

func abs_val() -> GdBigFloat:
	if sign >= 0:
		return self
	return neg()

func mul_pow2(k: int) -> GdBigFloat:
	if sign == 0:
		return self
	var r := GdBigFloat.new()
	r.sign = sign
	r.exp = exp + k
	r.limbs = limbs
	return r

func with_limbs(n: int) -> GdBigFloat:
	if limbs.size() == n:
		return self
	if sign == 0:
		return GdBigFloat.zero(n)
	return _normalize(limbs, exp, sign, n)

func to_float() -> float:
	if sign == 0:
		return 0.0
	var n := limbs.size()
	var v: float = float(limbs[n - 1]) * 268435456.0 + float(limbs[n - 2])
	if n >= 3:
		v = v * 268435456.0 + float(limbs[n - 3])
		return sign * v * pow(2.0, exp + BITS * (n - 3)) if exp + BITS * (n - 3) > -1000 else sign * (v * pow(2.0, exp + BITS * (n - 3) + 600)) * pow(2.0, -600)
	return sign * v * pow(2.0, exp + BITS * (n - 2))

# Exponent of the leading bit: value in [2^top_exp, 2^(top_exp+1))
func top_exp() -> int:
	return exp + BITS * limbs.size() - 1

func cmp_abs(o: GdBigFloat) -> int:
	if sign == 0:
		return 0 if o.sign == 0 else -1
	if o.sign == 0:
		return 1
	var te := top_exp()
	var to := o.top_exp()
	if te != to:
		return 1 if te > to else -1
	var a := limbs
	var b := o.limbs
	var na := a.size()
	var nb := b.size()
	var k := 0
	while k < maxi(na, nb):
		var x: int = a[na - 1 - k] if k < na else 0
		var y: int = b[nb - 1 - k] if k < nb else 0
		if x != y:
			return 1 if x > y else -1
		k += 1
	return 0

func cmp(o: GdBigFloat) -> int:
	if sign != o.sign:
		return 1 if sign > o.sign else -1
	if sign == 0:
		return 0
	return cmp_abs(o) * sign

func add(o: GdBigFloat) -> GdBigFloat:
	if o.sign == 0:
		return self
	if sign == 0:
		return o.with_limbs(limbs.size())
	var n := limbs.size()
	var big: GdBigFloat = self
	var small: GdBigFloat = o
	if cmp_abs(o) < 0:
		big = o
		small = self
	# working buffer: one guard limb below the n limbs of `big`
	var wb := PackedInt64Array()
	wb.resize(n + 2)
	var bl := big.limbs
	var bn := bl.size()
	for i in n:
		var src := bn - n + i
		wb[i + 1] = bl[src] if src >= 0 else 0
	var w_exp: int = big.exp + BITS * (bn - n) - BITS
	# align small into the same frame (bit p of ws has exponent w_exp + p)
	var ws := _place(small.limbs, small.exp, w_exp, n + 2)
	if big.sign == small.sign:
		var carry := 0
		for i in range(n + 2):
			var s: int = wb[i] + ws[i] + carry
			wb[i] = s & MASK
			carry = s >> BITS
		if carry > 0:
			wb.append(carry)
		return _normalize(wb, w_exp, big.sign, n)
	var borrow := 0
	for i in range(n + 2):
		var d: int = wb[i] - ws[i] - borrow
		if d < 0:
			d += BASE
			borrow = 1
		else:
			borrow = 0
		wb[i] = d
	return _normalize(wb, w_exp, big.sign, n)

func sub(o: GdBigFloat) -> GdBigFloat:
	return add(o.neg())

func mul(o: GdBigFloat) -> GdBigFloat:
	var n := limbs.size()
	if sign == 0 or o.sign == 0:
		return GdBigFloat.zero(n)
	var a := limbs
	var b := o.limbs
	var na := a.size()
	var nb := b.size()
	var cols := PackedInt64Array()
	cols.resize(na + nb)
	for i in na:
		var ai: int = a[i]
		if ai == 0:
			continue
		for j in nb:
			cols[i + j] += ai * b[j]
	var carry := 0
	for k in range(na + nb):
		var s: int = cols[k] + carry
		cols[k] = s & MASK
		carry = s >> BITS
	return _normalize(cols, exp + o.exp, sign * o.sign, n)

func mul_float(f: float) -> GdBigFloat:
	return mul(GdBigFloat.from_float(f, limbs.size()))

func div(o: GdBigFloat) -> GdBigFloat:
	var n := limbs.size()
	assert(o.sign != 0, "GdBigFloat division by zero")
	if sign == 0:
		return GdBigFloat.zero(n)
	var wp := n + 1
	var b := o.with_limbs(wp)
	# Newton reciprocal: x <- x (2 - b x), seeded from float64
	var e_b := b.top_exp()
	var b_scaled := b.mul_pow2(-e_b)  # in [1, 2)
	var x := GdBigFloat.from_float(1.0 / b_scaled.to_float(), wp)
	var two := GdBigFloat.from_int(2, wp)
	var correct_bits := 50
	while correct_bits < BITS * wp + 8:
		x = x.mul(two.sub(b_scaled.mul(x)))
		correct_bits *= 2
	var r := self.with_limbs(wp).mul(x).mul_pow2(-e_b)
	# exact correction: the magnitude becomes floor(|a| / |b|) at r's
	# exponent, so the result is the correctly truncated quotient (exact
	# whenever the quotient is representable, as in IEEE arithmetic)
	var sh: int = exp - o.exp - r.exp
	var N := _ishl(limbs, maxi(sh, 0))
	var D := _ishl(o.limbs, maxi(-sh, 0))
	var Q := _itrim(r.limbs)
	var QD := _imul(Q, D)
	while _icmp(QD, N) > 0:
		Q = _isub(Q, ONE_LIMB)
		QD = _isub(QD, D)
	var QD1 := _iadd(QD, D)
	while _icmp(QD1, N) <= 0:
		Q = _iadd(Q, ONE_LIMB)
		QD1 = _iadd(QD1, D)
	return _normalize(Q, r.exp, sign * o.sign, n)

func sqrt_val() -> GdBigFloat:
	var n := limbs.size()
	if sign == 0:
		return self
	assert(sign > 0, "GdBigFloat sqrt of negative")
	var wp := n + 1
	var a := self.with_limbs(wp)
	# scale to even exponent near 1
	var e := a.top_exp()
	if e % 2 != 0:
		e -= 1
	var a_s := a.mul_pow2(-e)  # in [1, 4)
	# Newton on inverse sqrt: y <- y (3 - a y^2) / 2
	var y := GdBigFloat.from_float(1.0 / sqrt(a_s.to_float()), wp)
	var three := GdBigFloat.from_int(3, wp)
	var correct_bits := 50
	while correct_bits < BITS * wp + 8:
		y = y.mul(three.sub(a_s.mul(y).mul(y))).mul_pow2(-1)
		correct_bits *= 2
	var r := a_s.mul(y).mul_pow2(e / 2)
	# exact correction: the magnitude becomes floor(sqrt(a)) at r's exponent
	# (exact for perfect squares)
	var sh: int = exp - 2 * r.exp
	var A := _ishl(limbs, maxi(sh, 0))
	var R := _itrim(r.limbs)
	while _icmp(_ishl(_imul(R, R), maxi(-sh, 0)), A) > 0:
		R = _isub(R, ONE_LIMB)
	var R1 := _iadd(R, ONE_LIMB)
	while _icmp(_ishl(_imul(R1, R1), maxi(-sh, 0)), A) <= 0:
		R = R1
		R1 = _iadd(R, ONE_LIMB)
	return _normalize(R, r.exp, 1, n)

# Bits of the integer `src` (exponent src_exp) re-expressed in a frame whose
# bit 0 has exponent frame_exp, truncated to `len` limbs.
static func _place(src: PackedInt64Array, src_exp: int, frame_exp: int, len: int) -> PackedInt64Array:
	var out := PackedInt64Array()
	out.resize(len)
	var sn := src.size()
	var b0: int = frame_exp - src_exp  # source bit index for target bit 0
	var q: int = b0 / BITS
	var r: int = b0 - q * BITS
	if r < 0:
		r += BITS
		q -= 1
	for i in len:
		var j: int = q + i
		if j >= sn:
			break
		var lo: int = 0
		if j >= 0:
			lo = src[j] >> r
		var hi: int = 0
		if r > 0 and j + 1 >= 0 and j + 1 < sn:
			hi = (src[j + 1] << (BITS - r)) & MASK
		out[i] = (lo | hi) & MASK
	return out

static func _normalize(arr: PackedInt64Array, e: int, s: int, n: int) -> GdBigFloat:
	var top := arr.size() - 1
	while top >= 0 and arr[top] == 0:
		top -= 1
	if top < 0:
		return GdBigFloat.zero(n)
	# bit length of top limb
	var t: int = arr[top]
	var bl := 0
	while t > 0:
		bl += 1
		t >>= 1
	var lshift: int = BITS - bl  # shift left so top limb has bit BITS-1 set
	var r := GdBigFloat.new()
	r.sign = s
	r.limbs = PackedInt64Array()
	r.limbs.resize(n)
	# result limb k (0..n-1) takes source position top - (n-1) + k, shifted
	for k in n:
		var src: int = top - (n - 1) + k
		var cur: int = arr[src] if src >= 0 else 0
		var below: int = arr[src - 1] if src - 1 >= 0 else 0
		var v: int = cur << lshift
		if lshift > 0:
			v |= below >> (BITS - lshift)
		r.limbs[k] = v & MASK
	r.exp = e + BITS * (top - (n - 1)) - lshift
	return r

# ---------------------------------------------------------------- exact integers
# Non-negative integers as little-endian limb arrays (base 2^BITS), used for
# the exact correction steps of div / sqrt_val.

const ONE_LIMB := [1]

static func _itrim(a) -> PackedInt64Array:
	var r := PackedInt64Array(a)
	var k := r.size()
	while k > 0 and r[k - 1] == 0:
		k -= 1
	r.resize(k)
	return r

static func _ishl(a: PackedInt64Array, bits: int) -> PackedInt64Array:
	var q: int = bits / BITS
	var rb: int = bits - q * BITS
	var out := PackedInt64Array()
	out.resize(a.size() + q + 1)
	var carry := 0
	for i in a.size():
		var v: int = (a[i] << rb) | carry
		out[i + q] = v & MASK
		carry = v >> BITS
	out[a.size() + q] = carry
	return _itrim(out)

static func _imul(a: PackedInt64Array, b: PackedInt64Array) -> PackedInt64Array:
	var cols := PackedInt64Array()
	cols.resize(a.size() + b.size() + 1)
	for i in a.size():
		var ai: int = a[i]
		if ai == 0:
			continue
		var carry := 0
		for j in b.size():
			var v: int = cols[i + j] + ai * b[j] + carry
			cols[i + j] = v & MASK
			carry = v >> BITS
		var k := i + b.size()
		while carry > 0:
			var v2: int = cols[k] + carry
			cols[k] = v2 & MASK
			carry = v2 >> BITS
			k += 1
	return _itrim(cols)

static func _icmp(a: PackedInt64Array, b: PackedInt64Array) -> int:
	var x := _itrim(a)
	var y := _itrim(b)
	if x.size() != y.size():
		return 1 if x.size() > y.size() else -1
	for k in range(x.size() - 1, -1, -1):
		if x[k] != y[k]:
			return 1 if x[k] > y[k] else -1
	return 0

static func _iadd(a, b) -> PackedInt64Array:
	var n := maxi(a.size(), b.size())
	var out := PackedInt64Array()
	out.resize(n + 1)
	var carry := 0
	for i in n:
		var v: int = (a[i] if i < a.size() else 0) + (b[i] if i < b.size() else 0) + carry
		out[i] = v & MASK
		carry = v >> BITS
	out[n] = carry
	return _itrim(out)

# a - b, a >= b
static func _isub(a, b) -> PackedInt64Array:
	var out := PackedInt64Array()
	out.resize(a.size())
	var borrow := 0
	for i in a.size():
		var v: int = a[i] - (b[i] if i < b.size() else 0) - borrow
		if v < 0:
			v += BASE
			borrow = 1
		else:
			borrow = 0
		out[i] = v
	return _itrim(out)

func _to_string() -> String:
	return "GdBigFloat(%s, %d limbs)" % [String.num(to_float(), 17), limbs.size()]
