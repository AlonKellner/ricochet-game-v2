extends SceneTree

# Goldens of the pure-GDScript stack (tag pre-native): the bit-exact outputs
# every native port must reproduce (docs/TDD_10_NATIVE_MIGRATION.md, 2.1).
#
#   godot --headless --path . -s res://tools/dump_goldens.gd -- [--out=res://bench/goldens]
#          [--check] [--part=bigfloat,exact,beam,fixtures,corpus,sweeps] [--sweep-stride=1]
#
# --check compares against the files in --out instead of writing them and
# exits 1 on any difference. Floats are hex bit patterns (little-endian bytes
# of PackedFloat64Array / PackedFloat32Array), BigFloats are
# "sign:exp:limb,limb,...", so "identical" means identical.

const PARTS := ["bigfloat", "exact", "beam", "fixtures", "corpus", "chain", "sweeps"]
const FIXTURE_FILES := ["stage81_rules", "stage82_portals", "stage83_projective", "stage85_flags",
	"stage86_ghost", "stage87_projective_plans", "stage87_click_forks"]
const CORPORA := ["", "stress", "scenes_corpus"]

var _out := "res://bench/goldens"
var _check := false
var _parts: Array = PARTS
var _stride := 1
var _failures := 0

func _init() -> void:
	for a in OS.get_cmdline_user_args():
		if a.begins_with("--out="):
			_out = a.trim_prefix("--out=")
		elif a == "--check":
			_check = true
		elif a.begins_with("--part="):
			_parts = a.trim_prefix("--part=").split(",")
		elif a.begins_with("--sweep-stride="):
			_stride = maxi(1, a.trim_prefix("--sweep-stride=").to_int())
	if not _check:
		DirAccess.make_dir_recursive_absolute(ProjectSettings.globalize_path(_out))
	for part in _parts:
		var t0 := Time.get_ticks_msec()
		var data: Dictionary
		match part:
			"bigfloat": data = _bigfloat()
			"exact": data = _exact()
			"beam": data = _beam()
			"fixtures": data = _fixtures()
			"corpus": data = _corpus()
			"chain": data = _chain()
			"sweeps": data = await _sweeps()
			_:
				push_error("unknown part " + part)
				_failures += 1
				continue
		_emit(part, data)
		print("[goldens] %s: %d keys, %.1f s" % [part, data.size(), (Time.get_ticks_msec() - t0) / 1000.0])
	if _check:
		print("[goldens] check %s: %d differences" % ["FAILED" if _failures > 0 else "passed", _failures])
	quit(1 if _failures > 0 else 0)

func _emit(part: String, data: Dictionary) -> void:
	var path := "%s/%s.json" % [_out, part]
	data["_schema"] = {"part": part, "floats": "hex of the little-endian bytes of PackedFloat64Array (f64) / PackedFloat32Array (Vector2)",
		"num": "f<hex> GdF64Num | d<hex><hex> GdDualNum (value, derivative) | b<sign:exp:limbs> GdBigFloat | null",
		"cn": "<num>/<num>", "event": EVENT_FIELDS, "beam": "p;tau;kappa", "generator": "tools/dump_goldens.gd"}
	if not _check:
		var f := FileAccess.open(path, FileAccess.WRITE)
		f.store_string(JSON.stringify(data, "", true, true))
		f.close()
		return
	var f := FileAccess.open(path, FileAccess.READ)
	if f == null:
		push_error("missing golden " + path)
		_failures += 1
		return
	var old = JSON.parse_string(f.get_as_text())
	if not (old is Dictionary):
		push_error("unreadable golden " + path)
		_failures += 1
		return
	var keys := {}
	for k in old:
		keys[k] = true
	for k in data:
		keys[k] = true
	var shown := 0
	for k in keys:
		# both sides through the same JSON round trip (parsed JSON holds every
		# number as float), so 1 and 1.0 compare equal
		var a := JSON.stringify(old.get(k), "", true, true)
		var b := JSON.stringify(JSON.parse_string(JSON.stringify(data.get(k), "", true, true)), "", true, true)
		if a != b:
			_failures += 1
			if shown < 10:
				shown += 1
				print("[goldens] %s: DIFF at %s" % [part, k])
				if part == "sweeps" and old.has(k) and data.has(k) and old[k] is Dictionary and data[k] is Dictionary:
					var ha: String = old[k].get("hashes", "")
					var hb: String = data[k].get("hashes", "")
					for i in mini(ha.length(), hb.length()) / 16:
						if ha.substr(i * 16, 16) != hb.substr(i * 16, 16):
							print("    first differing combo index %d (of %d)" % [i, hb.length() / 16])
							break

# ---------------------------------------------------------------- canonical forms

static func h64(x: float) -> String:
	return PackedFloat64Array([x]).to_byte_array().hex_encode()

static func h32v(v: Vector2) -> String:
	return PackedFloat32Array([v.x, v.y]).to_byte_array().hex_encode()

static func big(b: GdBigFloat) -> String:
	var ls := PackedStringArray()
	for l in b.limbs:
		ls.append(str(l))
	return "%d:%d:%s" % [b.sign, b.exp, ",".join(ls)]

static func num(x) -> String:
	if x == null:
		return "null"
	if x is float:
		return h64(x)
	if x is GdF64Num:
		return "f" + h64(x.v)
	if x is GdDualNum:
		return "d" + h64(x.v) + h64(x.d)
	if x is GdBigFloat:
		return "b" + big(x)
	if x is Array:
		return "[" + ",".join(x.map(func(y): return num(y))) + "]"
	if x is PackedFloat64Array:
		var out := PackedStringArray()
		for v in x:
			out.append(h64(v))
		return "[" + ",".join(out) + "]"
	return str(x)

static func cn(z: GdCN) -> String:
	return "null" if z == null else num(z.re) + "/" + num(z.im)

static func sha16(s: String) -> String:
	var ctx := HashingContext.new()
	ctx.start(HashingContext.HASH_SHA256)
	ctx.update(s.to_utf8_buffer())
	return ctx.finish().hex_encode().substr(0, 16)

# One event = one string of EVENT_FIELDS joined by ";" (flags are 0/1); beams
# are numbered by first appearance (object identity) and listed as
# "p;tau;kappa". A gain (GdFastSolver) is appended as a 21st field.
const EVENT_FIELDS := "kind;sid;left;applied;terminal;on_segment;rehit;popped;teleport;projective;state;anchored;seeded;epoch;beam;margin;point;t_from;t_to;flags[;gain]"

static func b(v: bool) -> String:
	return "1" if v else "0"

static func events(evs: Array, gains: Array = []) -> Dictionary:
	var beams: Array = []
	var ids := {}
	var out: Array = []
	for i in evs.size():
		var e: GdBeamSolver.Event = evs[i]
		var bid: int = e.beam.get_instance_id() if e.beam != null else 0
		if e.beam != null and not ids.has(bid):
			ids[bid] = beams.size()
			beams.append("%s;%s;%s" % [cn(e.beam.p), cn(e.beam.tau), num(e.beam.kappa)])
		var f := [e.kind, str(e.surface_id), b(e.left), b(e.applied), b(e.terminal), b(e.on_segment), b(e.rehit),
			b(e.popped), b(e.teleport), b(e.projective), b(e.state), b(e.anchored), b(e.seeded), str(e.epoch),
			str(ids.get(bid, -1)), h64(e.margin), cn(e.point), num(e.t_from), num(e.t_to), GdBeamTracer._flags_key(e.flags)]
		if i < gains.size():
			f.append(h64(gains[i]))
		out.append(";".join(f))
	return {"beams": beams, "events": out}

static func step_canon(s: GdTraceStep) -> String:
	var hit := "-"
	if s.hit != null:
		var h: GdTraceHit = s.hit
		var seg := "-"
		if h.segment != null:
			seg = h32v(h.segment.start.coords) + h32v(h.segment.end.coords)
		hit = "%s;%s;%s;%d;%s" % [h64(h.t), h32v(h.point.coords) if h.point != null else "-", seg, h.side, h.on_segment]
	return "%s,%s,%s,%d,%d,%d,%d,%s,%s,%s,%s,%s,%d,%s" % [h32v(s.start), h32v(s.end), h32v(s.via), s.frame_id,
		s.type, s.surface_id, s.hit_side, s.hit_on_segment, s.after_portal, s.ghost, s.is_arc_step,
		h64(s.est_error), s.precision_bits, hit]

static func path_canon(p: GdTracePath) -> String:
	if p == null:
		return "null"
	var steps := PackedStringArray()
	for s in p.steps:
		steps.append(step_canon(s))
	var targets: Array = p.targets_hit.keys()
	targets.sort()
	return "ci=%d;hc=%d;targets=%s;steps=%s" % [p.cursor_index, p.hit_count, str(targets), "|".join(steps)]

static func pair_canon(pair: GdBeamTracer.Pair) -> String:
	if pair == null:
		return "null"
	var r: GdPrecisionLadder.Result = pair.result
	return "P:%s\nH:%s\nF:%s\nR:%d,%d,%s,%s" % [path_canon(pair.planned), path_canon(pair.physical),
		GdBeamTracer._flags_key(pair.final_flags), r.planned_tier, r.physical_tier, r.converged, str(r.tiers_tried)]

static func f32(x: float) -> float:
	return Vector2(x, 0.0).x

# The floats of scene defs / a fixture as Godot parsed them (hex), keyed like
# the JSON: Godot's decimal parsing is not correctly rounded, so native tests
# must start from these bit patterns, not from the decimals.
static func inputs_of(defs: Array, extra: Dictionary) -> Dictionary:
	var surfs := []
	for d in defs:
		var o := {"id": int(d["id"]), "start": [h64(float(d["start"][0])), h64(float(d["start"][1]))],
			"end": [h64(float(d["end"][0])), h64(float(d["end"][1]))]}
		if d.get("via") != null:
			o["via"] = [h64(float(d["via"][0])), h64(float(d["via"][1]))]
		if d.get("circle") != null:
			o["circle"] = [h64(float(d["circle"]["center"][0])), h64(float(d["circle"]["center"][1])), h64(float(d["circle"]["radius"]))]
		for key in ["left", "right"]:
			var c = d.get(key)
			if c == null:
				continue
			var sd := {}
			if c.get("normal") != null:
				sd["normal"] = [h64(float(c["normal"][0])), h64(float(c["normal"][1]))]
			if c.has("theta"):
				sd["theta"] = h64(float(c["theta"]))
			if c.get("d") != null:
				sd["d"] = [h64(float(c["d"][0])), h64(float(c["d"][1]))]
			if c.get("state") != null and c["state"].get("value") is float:
				sd["state_value"] = h64(c["state"]["value"])
			if not sd.is_empty():
				o[key] = sd
		surfs.append(o)
	var out := {"surfaces": surfs}
	for k in extra:
		var v = extra[k]
		if v == null:
			continue
		if v is Array:
			out[k] = v.map(func(x): return h64(float(x)))
		else:
			out[k] = h64(float(v))
	return out

# ---------------------------------------------------------------- bigfloat

func _rand_float(rng: RandomNumberGenerator) -> float:
	return rng.randf_range(-1.0, 1.0) * pow(2.0, rng.randi_range(-40, 40))

func _bigfloat() -> Dictionary:
	var out := {}
	var rng := RandomNumberGenerator.new()
	rng.seed = 90
	for n in [2, 4, 8, 16, 32, 64]:
		for i in 40:
			var a: GdBigFloat
			var b: GdBigFloat
			var ia: String
			var ib: String
			if i % 4 == 3:
				ia = "%d.%09d%09d" % [rng.randi_range(-99999, 99999), rng.randi_range(0, 999999999), rng.randi_range(0, 999999999)]
				ib = "%d.%09d%09d" % [rng.randi_range(-99999, 99999), rng.randi_range(0, 999999999), rng.randi_range(0, 999999999)]
				a = GdBigFloat.from_string(ia, n)
				b = GdBigFloat.from_string(ib, n)
			else:
				var fa := _rand_float(rng)
				var fb := _rand_float(rng)
				if i % 7 == 0:
					fb = 0.0
				ia = h64(fa)
				ib = h64(fb)
				a = GdBigFloat.from_float(fa, n)
				b = GdBigFloat.from_float(fb, n)
			var k := rng.randi_range(-70, 70)
			var d := {"in_a": ia, "in_b": ib, "a": big(a), "b": big(b), "add": big(a.add(b)), "sub": big(a.sub(b)),
				"mul": big(a.mul(b)), "cmp": a.cmp(b), "cmp_abs": a.cmp_abs(b), "neg": big(a.neg()),
				"abs": big(a.abs_val()), "mul_pow2": big(a.mul_pow2(k)), "k": k, "to_float": h64(a.to_float()),
				"to_float_mul": h64(a.mul(b).to_float()), "with_limbs_half": big(a.with_limbs(maxi(2, n / 2))),
				"with_limbs_double": big(a.with_limbs(n * 2)), "sqrt_abs": big(a.abs_val().sqrt_val()),
				"top_exp": a.top_exp(), "is_zero": a.is_zero(), "is_negative": a.is_negative()}
			if not b.is_zero():
				d["div"] = big(a.div(b))
			out["%d/%02d" % [n, i]] = d
		var inf := GdBigFloat.infinity(n)
		out["%d/inf" % n] = {"inf": big(inf), "is_inf": inf.is_inf(), "to_float": h64(inf.to_float()),
			"cmp_one": inf.cmp(GdBigFloat.from_int(1, n)), "zero": big(GdBigFloat.zero(n)),
			"from_int": big(GdBigFloat.from_int(-123456789, n)), "limbs_for_bits": GdBigFloat.limbs_for_bits(n * 28 - 3)}
	return out

# ---------------------------------------------------------------- exact

func _dyadic(rng: RandomNumberGenerator) -> float:
	return rng.randi_range(-4096, 4096) / 16.0

func _ulp_up(x: float) -> float:
	return x * (1.0 + 2.220446049250313e-16) if x != 0.0 else 4.9e-324

func _exact() -> Dictionary:
	var out := {}
	var rng := RandomNumberGenerator.new()
	rng.seed = 91
	for i in 400:
		var ax := _dyadic(rng)
		var ay := _dyadic(rng)
		var bx := _dyadic(rng)
		var by := _dyadic(rng)
		var k: float = [0.5, 0.25, 2.0, -1.0, 1.5][i % 5]
		var cx := ax + k * (bx - ax)
		var cy := ay + k * (by - ay)
		var mode := i % 3        # 0 exact collinear, 1 perturbed by an ulp, 2 random
		if mode == 1:
			cx = _ulp_up(cx)
		elif mode == 2:
			cx = _dyadic(rng) + rng.randf()
			cy = _dyadic(rng) + rng.randf()
		var d := {"in": [h64(ax), h64(ay), h64(bx), h64(by), h64(cx), h64(cy)],
			"orient_zero": GdExact.orient_zero(ax, ay, bx, by, cx, cy)}
		# circle through (3,4,5) offsets: exactly on it, an ulp off it, random
		var r: float = [5.0, 10.0, 2.5][i % 3]
		var sc: float = r / 5.0
		var offs := [[3, 4], [-3, 4], [4, -3], [5, 0], [0, -5]]
		var o: Array = offs[i % 5]
		var kx: float = ax + o[0] * sc
		var ky: float = ay + o[1] * sc
		if mode == 1:
			kx = _ulp_up(kx)
		elif mode == 2:
			kx += rng.randf()
		d["on_circle_in"] = [h64(ax), h64(ay), h64(r), h64(kx), h64(ky)]
		d["on_circle_zero"] = GdExact.on_circle_zero(ax, ay, r, kx, ky)
		# four points on that circle (cocircular), one perturbed or random
		var p1 := Vector2(ax + 3 * sc, ay + 4 * sc)
		var p2 := Vector2(ax - 4 * sc, ay + 3 * sc)
		var p3 := Vector2(ax + 5 * sc, ay)
		var p4x: float = ax
		var p4y: float = ay - 5 * sc
		if mode == 1:
			p4y = _ulp_up(p4y)
		elif mode == 2:
			p4x += rng.randf() * 3.0
		d["incircle_in"] = [h64(p1.x), h64(p1.y), h64(p2.x), h64(p2.y), h64(p3.x), h64(p3.y), h64(p4x), h64(p4y)]
		d["incircle_zero"] = GdExact.incircle_zero(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y, p4x, p4y)
		# exact constructions
		var L := GdExact.line_through(ax, ay, bx, by)
		d["line_through"] = num(L)
		var Ld := GdExact.line_from_dir(ax, ay, bx - ax, by - ay)
		d["line_from_dir"] = num(Ld)
		var X := [GdExact.of(cx), GdExact.of(cy), GdExact.of(1.0)]
		d["reflect_point_h"] = num(GdExact.reflect_point_h(X, ax, ay, bx, by))
		var C := GdExact.circumcenter_h(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y)
		d["circumcenter_h"] = num(C)
		var rw2 := GdExact.of(r * r)
		var inv = GdExact.invert_point_h(X, [GdExact.of(ax), GdExact.of(ay), GdExact.of(1.0)], rw2)
		d["invert_point_h"] = num(inv)
		d["on_line_h"] = GdExact.on_line_h(L, X)
		d["reflect_line"] = num(GdExact.reflect_line(L, p1.x, p1.y, p2.x, p2.y))
		d["line_through_h"] = num(GdExact.line_through_h(bx, by, X))
		d["tangent_zero"] = GdExact.tangent_zero(L, [GdExact.of(ax), GdExact.of(ay), GdExact.of(1.0)], rw2)
		var line := GdExact.Line.from_exact(L)
		d["contains_c"] = line.contains(cx, cy)
		d["contains_b"] = line.contains(bx, by)
		d["concurrent"] = line.concurrent(p1.x, p1.y, p2.x, p2.y, p3.x, p3.y, p4x, p4y)
		d["reflected"] = num(line.reflected(p1.x, p1.y, p2.x, p2.y).exact())
		d["translated"] = num(line.translated(k, -k).exact())
		out["%03d" % i] = d
	return out

# ---------------------------------------------------------------- beam ops

const REFL := {"type": "reflection", "interactive": true}
const INV := {"type": "inversion", "interactive": true}

func _beam_defs(rng: RandomNumberGenerator) -> Array:
	var defs: Array = []
	var sid := 1
	for i in 3:
		var a := Vector2(f32(rng.randf_range(0, 1920)), f32(rng.randf_range(0, 1080)))
		var b := Vector2(f32(rng.randf_range(0, 1920)), f32(rng.randf_range(0, 1080)))
		defs.append({"id": sid, "line": true, "start": [a.x, a.y], "end": [b.x, b.y],
			"via": [f32((a.x + b.x) / 2.0), f32((a.y + b.y) / 2.0)], "full": false, "left": REFL, "right": REFL})
		sid += 1
	for i in 3:
		var c := Vector2(f32(rng.randf_range(200, 1700)), f32(rng.randf_range(200, 900)))
		var r := f32(rng.randf_range(50, 300))
		var a0 := rng.randf_range(0, TAU)
		var a1 := a0 + rng.randf_range(0.5, 4.0)
		var am := (a0 + a1) / 2.0
		defs.append({"id": sid, "line": false, "full": false,
			"start": [f32(c.x + r * cos(a0)), f32(c.y + r * sin(a0))], "end": [f32(c.x + r * cos(a1)), f32(c.y + r * sin(a1))],
			"via": [f32(c.x + r * cos(am)), f32(c.y + r * sin(am))], "left": REFL, "right": REFL})
		sid += 1
	for i in 2:
		var c := Vector2(f32(rng.randf_range(200, 1700)), f32(rng.randf_range(200, 900)))
		var r := f32(rng.randf_range(50, 300))
		defs.append({"id": sid, "line": false, "full": true, "circle": {"center": [c.x, c.y], "radius": r},
			"start": [c.x + r, c.y], "end": [c.x - r / 2, c.y - r * 0.75], "via": [c.x - r / 2, c.y + r * 0.75],
			"left": INV, "right": INV})
		sid += 1
	return defs

func _beam() -> Dictionary:
	var out := {}
	var rng := RandomNumberGenerator.new()
	rng.seed = 92
	var defs := _beam_defs(rng)
	out["defs"] = defs
	var f64 := GdNumCtx.f64()
	var big4 := GdNumCtx.with_limbs(4)
	var sc_f := GdBeamScene.from_defs(f64, defs)
	var sc_b := GdBeamScene.from_defs(big4, defs)
	for i in 200:
		var px := f32(rng.randf_range(0, 1920))
		var py := f32(rng.randf_range(0, 1080))
		var ang := rng.randf_range(0, TAU)
		var tx := cos(ang)
		var ty := sin(ang)
		var k: float = 0.0 if i % 3 == 0 else rng.randf_range(-0.02, 0.02)
		var t := rng.randf_range(-2000.0, 2000.0)
		var dt := rng.randf_range(-1.0, 1.0)
		var zx := f32(rng.randf_range(0, 1920))
		var zy := f32(rng.randf_range(0, 1080))
		var dx := cos(ang + 0.7)
		var dy := sin(ang + 0.7)
		var b := GdFastSolver.beam(px, py, tx, ty, k)
		var bs := GdFastSolver.seeded(b)
		var d := {"in": [h64(px), h64(py), h64(tx), h64(ty), h64(k), h64(t), h64(dt), h64(zx), h64(zy), h64(dx), h64(dy)],
			"fast_at": num(GdFastSolver.at(bs, t, dt)), "fast_at_view": num(GdFastSolver.at_view(b, t)),
			"fast_tangent_at": num(GdFastSolver.tangent_at(bs, t, dt)), "fast_param_of": h64(GdFastSolver.param_of(b, zx, zy)),
			"fast_antipode": num(GdFastSolver.antipode(b)) if k != 0.0 else "straight"}
		for ctx_name in ["f64", "big4"]:
			var ctx: GdNumCtx = f64 if ctx_name == "f64" else big4
			if ctx_name == "big4" and i % 4 != 0:
				continue
			var hb := GdHPBeam.make(ctx.cn(px, py), ctx.cn(tx, ty), ctx.num(k))
			var tt = ctx.num(t)
			d[ctx_name + "_at"] = cn(hb.at(tt))
			d[ctx_name + "_tangent_at"] = cn(hb.tangent_at(tt))
			d[ctx_name + "_param_of"] = num(hb.param_of(ctx.cn(zx, zy)))
			if k != 0.0:
				d[ctx_name + "_at_infinity"] = cn(hb.at_infinity())
		var per_surf := []
		for si in sc_f.surfaces.size():
			var s: GdBeamScene.Surf = sc_f.surfaces[si]
			var sd := {"id": s.id, "fast_hits": num(GdFastSolver.surf_hits(bs, s, false)),
				"fast_on_segment": GdFastSolver.on_segment(s, zx, zy), "fast_side_left": GdFastSolver.side_left(s, zx, zy, dx, dy),
				"fast_endpoint_at": [GdFastSolver.endpoint_at(s, zx, zy), GdFastSolver.endpoint_at(s, s.sx, s.sy), GdFastSolver.endpoint_at(s, s.ex, s.ey)],
				"fast_blocked_sides": str(GdFastSolver.blocked_sides(s, 1, dx, dy)) + str(GdFastSolver.blocked_sides(s, 2, dx, dy)),
				"fast_apply": num(GdFastSolver.apply_surf(bs, t, dt, s))}
			var hits := GdFastSolver.surf_hits(bs, s, false)
			if hits.size() >= 2:
				sd["fast_apply_at_root"] = num(GdFastSolver.apply_surf(bs, hits[0], hits[1], s))
			for ctx_name in ["f64", "big4"]:
				var ctx: GdNumCtx = f64 if ctx_name == "f64" else big4
				if ctx_name == "big4" and i % 4 != 0:
					continue
				var scx: GdBeamScene = sc_f if ctx_name == "f64" else sc_b
				var sx: GdBeamScene.Surf = scx.surfaces[si]
				var hb := GdHPBeam.make(ctx.cn(px, py), ctx.cn(tx, ty), ctx.num(k))
				var z := ctx.cn(zx, zy)
				var dir := ctx.cn(dx, dy)
				var roots: Array = GdBeamGeom.hits(hb, sx.seg, false)
				sd[ctx_name + "_hits"] = num(roots)
				sd[ctx_name + "_on_segment"] = GdBeamGeom.on_segment(sx.seg, z)
				sd[ctx_name + "_side_left"] = GdBeamGeom.side_left(sx.seg, z, dir)
				sd[ctx_name + "_endpoint_at"] = GdBeamGeom.endpoint_at(sx.seg, z)
				sd[ctx_name + "_blocked_sides"] = str(GdBeamGeom.blocked_sides(sx.seg, 1, dir))
				var ap := GdBeamEffects.apply(hb, ctx.num(t), sx, sx.effect(true))
				sd[ctx_name + "_apply"] = cn(ap.p) + ";" + cn(ap.tau) + ";" + num(ap.kappa)
				if roots.size() >= 1:
					var ar := GdBeamEffects.apply(hb, roots[0], sx, sx.effect(true))
					sd[ctx_name + "_apply_at_root"] = cn(ar.p) + ";" + cn(ar.tau) + ";" + num(ar.kappa)
			per_surf.append(sd)
		d["surfaces"] = per_surf
		out["%03d" % i] = d
	return out

# ---------------------------------------------------------------- fixtures

static func plan_of(j: Array) -> Array:
	var out: Array = []
	for e in j:
		var c = e.get("click")
		out.append(GdBeamSolver.PlanItem.make(int(e["surface_id"]), e["side"] == "LEFT",
			null if c == null else Vector2(c[0], c[1])))
	return out

func _walk_generic(fx: Dictionary, ctx: GdNumCtx) -> Array:
	var scene := GdBeamScene.from_defs(ctx, fx["surfaces"])
	var player := ctx.cn(fx["player"][0], fx["player"][1])
	var cursor = null if fx["cursor"] == null else ctx.cn(fx["cursor"][0], fx["cursor"][1])
	var mode: int = GdBeamSolver.Mode.PHYSICAL if fx["mode"] == "physical" else GdBeamSolver.Mode.PLANNED
	var plan := plan_of(fx["plan"])
	var flags: Dictionary = fx.get("flags", {})
	if fx["direction"] != null:
		return GdBeamSolver.trace_direction(ctx, scene, player, ctx.cn(fx["direction"][0], fx["direction"][1]), plan, cursor, mode,
			GdBeamSolver.MAX_HITS, flags)
	if mode == GdBeamSolver.Mode.PLANNED:
		return GdBeamSolver.planned(ctx, scene, player, cursor, plan, GdBeamSolver.MAX_HITS, flags)
	return GdBeamSolver.physical(ctx, scene, player, cursor, plan, GdBeamSolver.MAX_HITS, flags)

func _walk_fast(fx: Dictionary) -> GdFastSolver.Out:
	var sc := GdFastSolver.Scene.from_defs(fx["surfaces"])
	var player := PackedFloat64Array([fx["player"][0], fx["player"][1]])
	var cursor = null if fx["cursor"] == null else PackedFloat64Array([fx["cursor"][0], fx["cursor"][1]])
	var mode: int = GdBeamSolver.Mode.PHYSICAL if fx["mode"] == "physical" else GdBeamSolver.Mode.PLANNED
	var plan := plan_of(fx["plan"])
	var flags: Dictionary = fx.get("flags", {})
	if fx["direction"] != null:
		return GdFastSolver.trace_direction(sc, player, PackedFloat64Array([fx["direction"][0], fx["direction"][1]]), plan, cursor, mode,
			GdBeamSolver.MAX_HITS, flags)
	if mode == GdBeamSolver.Mode.PLANNED:
		return GdFastSolver.planned(sc, player, cursor, plan, GdBeamSolver.MAX_HITS, flags)
	return GdFastSolver.physical(sc, player, cursor, plan, GdBeamSolver.MAX_HITS, flags)

func _fixtures() -> Dictionary:
	var out := {}
	for fname in FIXTURE_FILES:
		var f := FileAccess.open("res://bench/fixtures/%s.json" % fname, FileAccess.READ)
		var d: Dictionary = JSON.parse_string(f.get_as_text())
		for fx in d["fixtures"]:
			var key := "%s/%s" % [fname, fx["name"]]
			var fast := _walk_fast(fx)
			var clicks := []
			for e in fx["plan"]:
				var c = e.get("click")
				clicks.append(null if c == null else [h64(float(c[0])), h64(float(c[1]))])
			var inputs := inputs_of(fx["surfaces"], {"player": fx["player"], "cursor": fx["cursor"], "direction": fx["direction"]})
			inputs["clicks"] = clicks
			out[key] = {"inputs": inputs, "f64": events(_walk_generic(fx, GdNumCtx.f64())), "fast": events(fast.events, fast.gain),
				"big4": events(_walk_generic(fx, GdNumCtx.with_limbs(4)))}
	return out

# ---------------------------------------------------------------- corpora

func _corpus() -> Dictionary:
	var out := {}
	for sub in CORPORA:
		PrecisionBench.use_corpus(sub)
		for name in PrecisionBench.list_scenarios():
			var scn: Dictionary = PrecisionBench.load_json("%s/%s.json" % [PrecisionBench.SCN_DIR, name])
			var defs: Array = PrecisionBench.scene_defs(scn)
			var player := PackedFloat64Array([float(scn["player"][0]), float(scn["player"][1])])
			var cursor := PackedFloat64Array([float(scn["cursor"][0]), float(scn["cursor"][1])])
			var plan := PrecisionBench.plan_items(scn)
			var fsc := GdFastSolver.Scene.from_defs(defs)
			var fp := GdFastSolver.planned(fsc, player, cursor, plan)
			var fh := GdFastSolver.physical(fsc, player, cursor, plan)
			var r := GdPrecisionLadder.solve(defs, [player[0], player[1]], [cursor[0], cursor[1]], plan, 32, 32, 0.01, false, fsc)
			var big4 := GdNumCtx.with_limbs(4)
			var scb := GdBeamScene.from_defs(big4, defs)
			var pb := big4.cn(player[0], player[1])
			var cb := big4.cn(cursor[0], cursor[1])
			out["%s/%s" % [sub if sub != "" else "main", name]] = {
				"inputs": inputs_of(defs, {"player": [player[0], player[1]], "cursor": [cursor[0], cursor[1]]}),
				"fast_planned": events(fp.events, fp.gain), "fast_physical": events(fh.events, fh.gain),
				"ladder": {"planned": events(r.planned), "planned_err": num(r.planned_err), "planned_tier": r.planned_tier,
					"physical": events(r.physical), "physical_err": num(r.physical_err), "physical_tier": r.physical_tier,
					"converged": r.converged, "tiers_tried": r.tiers_tried, "planned_from_physical": r.planned_from_physical},
				"big4_planned": events(GdBeamSolver.planned(big4, scb, pb, cb, plan)),
				"big4_physical": events(GdBeamSolver.physical(big4, scb, pb, cb, plan)),
			}
	PrecisionBench.use_corpus("")
	return out

# ---------------------------------------------------------------- anchored chains

static func beam_str(b: GdHPBeam) -> String:
	return "%s;%s;%s" % [cn(b.p), cn(b.tau), num(b.kappa)]

# The anchored chain (B_0..B_K) of every main corpus scenario at f64 and 4
# limbs, plus the backward image chain and pole chain of each plan prefix.
func _chain() -> Dictionary:
	var out := {}
	PrecisionBench.use_corpus("")
	for name in PrecisionBench.list_scenarios():
		var scn: Dictionary = PrecisionBench.load_json("%s/%s.json" % [PrecisionBench.SCN_DIR, name])
		var defs: Array = PrecisionBench.scene_defs(scn)
		var plan := PrecisionBench.plan_items(scn)
		var d := {"inputs": inputs_of(defs, {"player": [float(scn["player"][0]), float(scn["player"][1])],
			"cursor": [float(scn["cursor"][0]), float(scn["cursor"][1])]})}
		for ctx_name in ["f64", "big4"]:
			var ctx: GdNumCtx = GdNumCtx.f64() if ctx_name == "f64" else GdNumCtx.with_limbs(4)
			var scene := GdBeamScene.from_defs(ctx, defs)
			var player := ctx.cn(float(scn["player"][0]), float(scn["player"][1]))
			var cursor := ctx.cn(float(scn["cursor"][0]), float(scn["cursor"][1]))
			var chain := GdBeamSolver.AnchorChain.build(scene, player, cursor, plan, {})
			if chain == null:
				d[ctx_name] = null
				continue
			var beams := []
			var ids := {}
			var idx := []
			for b in chain.beams:
				var k: int = b.get_instance_id()
				if not ids.has(k):
					ids[k] = beams.size()
					beams.append(beam_str(b))
				idx.append(ids[k])
			# the image chain and the poles of the full plan, step by step
			var X = cursor
			var images := []
			for j in range(plan.size() - 1, -1, -1):
				X = GdBeamEffects.map_point(X, scene.by_id[plan[j].surface_id], true)
				images.append("null" if X == null else cn(X))
				if X == null:
					break
			var pole: Array = [player, null, null]
			var poles := []
			for j in plan.size():
				pole = GdBeamEffects.map_pole(pole, scene.by_id[plan[j].surface_id])
				poles.append("%s|%s|%s" % ["null" if pole[0] == null else cn(pole[0]), "null" if pole[1] == null else cn(pole[1]),
					"null" if pole[2] == null else cn(pole[2])])
				if pole[0] == null:
					break
			d[ctx_name] = {"beams": beams, "index": idx, "images": images, "poles": poles}
		out[name] = d
	return out

# ---------------------------------------------------------------- sweeps

func _instantiate(path: String) -> Node:
	printerr("[goldens] sweeps: loading %s" % path)
	Surface.reset_id_counter()
	GdBeamTracer.clear_cache()
	var scene: Node = load(path).instantiate()
	scene.gravity = Vector2.ZERO
	root.add_child(scene)
	return scene

func _release(scene: Node) -> void:
	root.remove_child(scene)
	scene.free()

func _plan_key(plan: Array) -> String:
	if plan.is_empty():
		return "[]"
	return "[" + ",".join(plan.map(func(e): return "%d/%s" % [e.surface_id, "L" if e.side == Side.Value.LEFT else "R"])) + "]"

static func h32(x: float) -> String:
	return PackedFloat32Array([x]).to_byte_array().hex_encode()

# All (player, cursor) combos of `positions` under `plan`, hashed in order,
# with the scene defs (parsed floats as hex), the positions (float32 hex) and
# the plan, so a native tracer can reproduce every combo.
func _sweep_group(out: Dictionary, key: String, scene: Node, positions: Array, plans: Array, skip_equal: bool) -> void:
	var flags: Dictionary = {}
	var defs: Array = GdBeamScene.defs_from_surfaces(scene.surfaces)
	var pos_hex := []
	for p in positions:
		pos_hex.append([h32(p.x), h32(p.y)])
	var inputs := inputs_of(defs, {})
	inputs["defs"] = defs
	inputs["positions"] = pos_hex
	for plan in plans:
		var hashes := PackedStringArray()
		var samples := {}
		var idx := 0
		for p in positions:
			for c in positions:
				if skip_equal and p == c:
					continue
				var i := idx
				idx += 1
				if i % _stride != 0:
					continue
				var pair := GdBeamTracer.trace_pair(scene.surfaces, p, c, plan, flags)
				var canon := pair_canon(pair)
				hashes.append(sha16(canon))
				if i == 0 or i == 977:
					samples[str(i)] = canon
		out["%s/%s" % [key, _plan_key(plan)]] = {"positions": positions.size(), "combos": idx, "stride": _stride,
			"skip_equal": skip_equal, "plan": plan.map(func(e): return {"surface_id": e.surface_id, "side": "LEFT" if e.side == Side.Value.LEFT else "RIGHT"}),
			"inputs": inputs, "hashes": "".join(hashes), "samples": samples}
		printerr("[goldens] sweeps: %s/%s: %d combos" % [key, _plan_key(plan), idx])

func _sweeps() -> Dictionary:
	var out := {}
	printerr("[goldens] sweeps: start (stride %d)" % _stride)
	# test_sweep_all_scenes: every test level, no plan, SweepRunner.sweep bounds, all pairs
	for path in SweepPlans.discover_scenes(SweepPlans.TEST_LEVELS_DIR):
		var scene := _instantiate(path)
		await process_frame
		var runner := SweepRunner.new().configure(5, 10, 42)
		var has_room: bool = "room_rect" in scene and "build_room" in scene and scene.build_room
		var has_blocks: bool = "block_lines" in scene and scene.block_lines.size() > 0
		if has_room or has_blocks:
			var rect: Rect2 = scene.room_rect if "room_rect" in scene else Rect2(560, 240, 800, 600)
			runner.set_bounds(rect.position + Vector2(10, 10), rect.position + rect.size - Vector2(10, 10))
		var positions := runner.build_positions(runner._extract_points_of_interest(scene))
		_sweep_group(out, "all_scenes/" + path.get_file().get_basename(), scene, positions, [[]], false)
		_release(scene)
	# test_sweep_with_plans
	for path in SweepPlans.plan_scenes():
		var scene := _instantiate(path)
		await process_frame
		var plans := SweepPlans.reflective_plans(scene)
		plans.append_array(SweepPlans.inversion_plans(scene))
		if plans.is_empty():
			_release(scene)
			continue
		var runner := SweepPlans.bounded_runner(scene, 5, 10, 42)
		var positions := runner.build_positions(runner._extract_points_of_interest(scene))
		_sweep_group(out, "with_plans/" + path.get_file().get_basename(), scene, positions, plans, true)
		_release(scene)
	# test_sweep_projective_plans
	for path in SweepPlans.projective_scenes():
		var scene := _instantiate(path)
		await process_frame
		var runner := SweepPlans.bounded_runner(scene, 4, 6, 87)
		var positions := runner.build_positions()
		_sweep_group(out, "projective_plans/" + path.get_file().get_basename(), scene, positions, SweepPlans.projective_plans(scene), true)
		_release(scene)
	# test_precision_invariant_sweep
	var grid := _instantiate(SweepPlans.PRECISION_SCENE)
	await process_frame
	var prunner := SweepRunner.new().configure(5, 10, 42)
	_sweep_group(out, "precision/no_plan", grid, prunner.build_positions(prunner._extract_points_of_interest(grid)), [[]], true)
	_sweep_group(out, "precision/plans", grid, SweepPlans.precision_positions(), SweepPlans.precision_plans(), true)
	_release(grid)
	# test_sweep_surface_combos
	for combo in SweepPlans.combo_configs():
		var scene := SweepPlans.build_combo_scene(combo)
		GdBeamTracer.clear_cache()
		root.add_child(scene)
		await process_frame
		var runner := SweepPlans.bounded_runner(scene, 5, 10, 42)
		var positions := runner.build_positions(runner._extract_points_of_interest(scene))
		_sweep_group(out, "combos/" + combo.label.replace(" ", "").replace("/", "_"), scene, positions, [[]], true)
		_release(scene)
	return out
