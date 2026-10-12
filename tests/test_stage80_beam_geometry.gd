extends GutTest

# Stage 80: precision-generic segment geometry for the beam solver.
# Expected values: closed forms, or the mpmath oracle fixture
# bench/fixtures/stage80_geometry.json (docs/research/oracle/gen_fixtures_stage80.py).

const FIXTURE := "res://bench/fixtures/stage80_geometry.json"

var _fx: Dictionary

func before_all():
	var f := FileAccess.open(FIXTURE, FileAccess.READ)
	_fx = JSON.parse_string(f.get_as_text())

func _beam(ctx: NumCtx, bj: Dictionary) -> HPBeam:
	return HPBeam.make(ctx.cn(bj["p"][0], bj["p"][1]), ctx.cn(bj["tau"][0], bj["tau"][1]), ctx.num(bj["kappa"]))

func _seg(ctx: NumCtx, sid: int) -> BeamGeom.Seg:
	for sj in _fx["segments"]:
		if int(sj["id"]) == sid:
			return BeamGeom.seg_from_dict(ctx, sj)
	return null

func _line(ctx: NumCtx, a: Vector2, b: Vector2) -> BeamGeom.Seg:
	return BeamGeom.seg_from_dict(ctx, {"kind": "line", "id": 1, "start": [a.x, a.y], "end": [b.x, b.y],
		"via": [(a.x + b.x) / 2.0, (a.y + b.y) / 2.0]})

func _sorted_floats(ts: Array) -> Array:
	var out: Array = ts.map(func(t): return t.to_float())
	out.sort()
	return out

# ---------------------------------------------------------------- hits

func test_line_hits_closed_form():
	for ctx: NumCtx in [NumCtx.f64(), NumCtx.with_limbs(4)]:
		var vline := _line(ctx, Vector2(7, -50), Vector2(7, 50))
		var hline := _line(ctx, Vector2(-50, 9), Vector2(50, 9))
		var b1 := HPBeam.make(ctx.cn(0, 0), ctx.cn(1, 0), ctx.num(0))
		var b2 := HPBeam.make(ctx.cn(0, 3), ctx.cn(-1, 0), ctx.num(0))
		var b3 := HPBeam.make(ctx.cn(4, 1), ctx.cn(0, 1), ctx.num(0))
		assert_eq(_sorted_floats(BeamGeom.hits(b1, vline, false)), [7.0], "%s: +x to x=7" % ctx.name())
		assert_eq(_sorted_floats(BeamGeom.hits(b2, vline, false)), [-7.0], "%s: -x from (0,3)" % ctx.name())
		assert_eq(_sorted_floats(BeamGeom.hits(b3, hline, false)), [8.0], "%s: +y to y=9" % ctx.name())
		assert_eq(BeamGeom.hits(b1, hline, false).size(), 0, "%s: parallel line, no root" % ctx.name())
		var obl := _line(ctx, Vector2(10, -100), Vector2(10, 100))
		var b4 := HPBeam.make(ctx.cn(1, 2), ctx.cn(0.6, 0.8), ctx.num(0))
		var r := _sorted_floats(BeamGeom.hits(b4, obl, false))
		assert_eq(r.size(), 1)
		if r.size() == 1:
			assert_almost_eq(r[0], 15.0, 1e-13, "%s: oblique t = 9/0.6" % ctx.name())

func test_arc_beam_vs_line():
	# every fixture case (lines, arcs, full circles): roots' points, on-segment
	# and side agree with the oracle
	var checked := 0
	for ctx: NumCtx in [NumCtx.f64(), NumCtx.with_limbs(4)]:
		var tol: float = 1e-6 if ctx.is_f64() else 1e-9
		for case in _fx["cases"]:
			var bj: Dictionary = _fx["beams"][int(case["beam"])]
			var b := _beam(ctx, bj)
			var sg := _seg(ctx, int(case["seg"]))
			var recs := BeamGeom.hit_records(b, sg, false)
			var exp: Array = case["hits"]
			assert_eq(recs.size(), exp.size(), "%s beam %d seg %d: root count" % [ctx.name(), case["beam"], case["seg"]])
			if recs.size() != exp.size():
				continue
			# match by point (root order is not part of the contract)
			for ex in exp:
				var ep := Vector2(String(ex["x"]).to_float(), String(ex["y"]).to_float())
				var best = null
				var best_d := INF
				for rr in recs:
					var d: float = Vector2(rr["point"].fx(), rr["point"].fy()).distance_to(ep)
					if d < best_d:
						best_d = d
						best = rr
				assert_lt(best_d, tol, "%s beam %d seg %d: root point" % [ctx.name(), case["beam"], case["seg"]])
				assert_eq(best["on_segment"], bool(ex["on_segment"]), "%s beam %d seg %d: on-segment" % [ctx.name(), case["beam"], case["seg"]])
				assert_eq(BeamGeom.side_left(sg, best["point"], b.tangent_at(best["t"])), ex["side"] == "LEFT",
					"%s beam %d seg %d: side" % [ctx.name(), case["beam"], case["seg"]])
				checked += 1
	assert_gt(checked, 300, "fixture coverage")

func test_tangent_line_double_root():
	# circle beam of radius 8 centered (0,8) through the origin, heading +x
	# (κ = 0.125 is exact in binary); the line x = 8 touches it at (8,8), t = 16
	for ctx: NumCtx in [NumCtx.f64(), NumCtx.with_limbs(4)]:
		var b := HPBeam.make(ctx.cn(0, 0), ctx.cn(1, 0), ctx.num(0.125))
		var recs := BeamGeom.hit_records(b, _line(ctx, Vector2(8, -50), Vector2(8, 50)), false)
		assert_gt(recs.size(), 0, "%s: tangency found" % ctx.name())
		for rr in recs:
			assert_almost_eq(Vector2(rr["point"].fx(), rr["point"].fy()), Vector2(8, 8), Vector2(1e-12, 1e-12))

# ---------------------------------------------------------------- on-segment, endpoints

func test_on_segment_exact_at_endpoints():
	for ctx: NumCtx in [NumCtx.f64(), NumCtx.with_limbs(4), NumCtx.with_limbs(8)]:
		var sg := _line(ctx, Vector2(0, 0), Vector2(10, 0))
		var E: CN = ctx.cn(10, 0)
		assert_true(BeamGeom.on_segment(sg, E), "%s: endpoint is on-segment" % ctx.name())
		assert_eq(BeamGeom.endpoint_at(sg, E), 2, "%s: exactly the end endpoint" % ctx.name())
		var outside: CN = ctx.cn(10.000000000000002, 0)   # 1 ulp beyond the end
		assert_false(BeamGeom.on_segment(sg, outside), "%s: 1 ulp outside is off-segment" % ctx.name())
		assert_eq(BeamGeom.endpoint_at(sg, outside), 0)
		# provenance: a beam constructed through the endpoint snaps to it
		var b := HPBeam.make(ctx.cn(-5, 0), ctx.cn(1, 0), ctx.num(0))
		var recs := BeamGeom.hit_records(b, _line(ctx, Vector2(10, -3), Vector2(10, 0)), false, [[E, ctx.num(15)]])
		assert_eq(recs.size(), 1)
		if recs.size() == 1:
			assert_eq(recs[0]["at_endpoint"], 2, "%s: provenance endpoint" % ctx.name())
			assert_true(recs[0]["on_segment"])
			assert_eq(recs[0]["point"].fx(), 10.0)
			assert_eq(recs[0]["point"].fy(), 0.0)

func test_on_segment_generic_matches_legacy():
	var rng := RandomNumberGenerator.new()
	rng.seed = 801
	var hp := NumCtx.with_limbs(8)
	var compared := 0
	for i in 1000:
		var s := Vector2(rng.randi_range(100, 1800), rng.randi_range(100, 1000))
		var e := Vector2(rng.randi_range(100, 1800), rng.randi_range(100, 1000))
		var arc := i % 2 == 1
		var v := (s + e) / 2.0 if not arc else Vector2(rng.randi_range(100, 1800), rng.randi_range(100, 1000))
		var legacy_seg := Segment.from_coords(s, e, v)
		var d := {"kind": "arc" if arc else "line", "id": 1, "start": [s.x, s.y], "end": [e.x, e.y], "via": [v.x, v.y]}
		var p := Vector2(rng.randi_range(0, 1920), rng.randi_range(0, 1080))
		var val_hp = BeamGeom.on_segment_value(BeamGeom.seg_from_dict(hp, d), hp.cn(p.x, p.y))
		if val_hp == null:
			assert_true(false, "on_segment_value implemented")
			return
		# skip points within 1e-6 (relative) of the decision boundary: the
		# legacy test runs in float32 Vector2 arithmetic
		var mag: float = absf(val_hp.to_float())
		var scale: float = pow(maxf(s.length(), e.length()) + p.length(), 4)
		if mag < 1e-6 * scale:
			continue
		var gen := BeamGeom.on_segment(BeamGeom.seg_from_dict(NumCtx.f64(), d), NumCtx.f64().cn(p.x, p.y))
		assert_eq(gen, Intersection.is_on_segment(p, legacy_seg), "case %d" % i)
		compared += 1
	assert_gt(compared, 900)

func test_side_visual_space():
	# legacy side in the identity frame (no conjugation flip) on clean cases
	var rng := RandomNumberGenerator.new()
	rng.seed = 802
	var ctx := NumCtx.f64()
	var compared := 0
	for i in 400:
		var s := Vector2(rng.randi_range(100, 1800), rng.randi_range(100, 1000))
		var e := Vector2(rng.randi_range(100, 1800), rng.randi_range(100, 1000))
		var arc := i % 2 == 1
		var v := (s + e) / 2.0 if not arc else Vector2(rng.randi_range(100, 1800), rng.randi_range(100, 1000))
		var legacy_seg := Segment.from_coords(s, e, v)
		var d := {"kind": "arc" if arc else "line", "id": 1, "start": [s.x, s.y], "end": [e.x, e.y], "via": [v.x, v.y]}
		var p := s.lerp(e, 0.37) if not arc else legacy_seg.start.coords
		var ang := rng.randf_range(0, TAU)
		var dir := Vector2(cos(ang), sin(ang))
		var g: Vector2
		var carrier := legacy_seg.get_carrier()
		g = Vector2(2.0 * carrier.a * p.x + carrier.b, 2.0 * carrier.a * p.y + carrier.c)
		if absf(dir.dot(g)) < 1e-3 * g.length():
			continue   # near-tangent approach: skip (legacy is float32)
		var ray := Ray.from_coords(p - dir, Direction.from_coords(p - dir, p))
		var legacy_left: bool = Intersection._determine_side(ray, p, legacy_seg) == Side.Value.LEFT
		var gen_left := BeamGeom.side_left(BeamGeom.seg_from_dict(ctx, d), ctx.cn(p.x, p.y), ctx.cn(dir.x, dir.y))
		assert_eq(gen_left, legacy_left, "case %d (%s)" % [i, "arc" if arc else "line"])
		compared += 1
	assert_gt(compared, 300)

func test_endpoint_blocked_sides_ported():
	var rng := RandomNumberGenerator.new()
	rng.seed = 803
	var ctx := NumCtx.f64()
	var compared := 0
	for i in 400:
		var s := Vector2(rng.randi_range(100, 1800), rng.randi_range(100, 1000))
		var e := Vector2(rng.randi_range(100, 1800), rng.randi_range(100, 1000))
		var arc := i % 2 == 1
		var v := (s + e) / 2.0 if not arc else Vector2(rng.randi_range(100, 1800), rng.randi_range(100, 1000))
		var legacy_seg := Segment.from_coords(s, e, v)
		var d := {"kind": "arc" if arc else "line", "id": 1, "start": [s.x, s.y], "end": [e.x, e.y], "via": [v.x, v.y]}
		var which := 1 + (i % 2)
		var ep := s if which == 1 else e
		var ang := rng.randf_range(0, TAU)
		var dir := Vector2(cos(ang), sin(ang))
		var tangent := Intersection.tangent_into_segment(legacy_seg, which)
		if absf(dir.cross(tangent)) < 1e-3:
			continue
		var ray := Ray.from_coords(ep - dir, Direction.from_coords(ep - dir, ep))
		var legacy: Array = Intersection.endpoint_blocked_sides(ep, legacy_seg, ray, which)
		var gen := BeamGeom.blocked_sides(BeamGeom.seg_from_dict(ctx, d), which, ctx.cn(dir.x, dir.y))
		assert_eq(gen, legacy, "case %d" % i)
		compared += 1
	# exact cases (closed form): grazing perpendicular at the start of (0,0)-(10,0)
	var sg := _line(ctx, Vector2(0, 0), Vector2(10, 0))
	assert_eq(BeamGeom.blocked_sides(sg, 1, ctx.cn(0, 1)), [true, false])
	assert_eq(BeamGeom.blocked_sides(sg, 1, ctx.cn(0, -1)), [false, true])
	assert_eq(BeamGeom.blocked_sides(sg, 1, ctx.cn(1, 0)), [true, true])
	assert_gt(compared, 300)

# ---------------------------------------------------------------- FastSolver twin

func test_fast_twin_lines():
	var gen_ctx := NumCtx.f64()
	var dual_ctx := NumCtx.dual()
	var compared := 0
	for case in _fx["cases"]:
		var sj: Dictionary = {}
		for x in _fx["segments"]:
			if int(x["id"]) == int(case["seg"]):
				sj = x
		if sj["kind"] != "line":
			continue
		var bj: Dictionary = _fx["beams"][int(case["beam"])]
		var gen := _sorted_floats(BeamGeom.hits(_beam(gen_ctx, bj), BeamGeom.seg_from_dict(gen_ctx, sj), false))
		var raw := FastSolver.seeded(FastSolver.beam(bj["p"][0], bj["p"][1], bj["tau"][0], bj["tau"][1], bj["kappa"]))
		var fast := FastSolver.line_hits(raw, sj["start"][0], sj["start"][1], sj["end"][0], sj["end"][1], false)
		var fts: Array = []
		var fdt := {}
		var q := 0
		while q < fast.size():
			fts.append(fast[q])
			fdt[fast[q]] = fast[q + 1]
			q += 2
		fts.sort()
		assert_eq(gen.size(), case["hits"].size(), "beam %d seg %d: generic root count = oracle" % [case["beam"], case["seg"]])
		assert_eq(fts, gen, "beam %d seg %d: bit-identical roots" % [case["beam"], case["seg"]])
		# dual derivative vs DualNum generic
		var db := FastSolver.seeded(FastSolver.beam(bj["p"][0], bj["p"][1], bj["tau"][0], bj["tau"][1], bj["kappa"]))
		var dbeam := HPBeam.make(CN.make(DualNum.of(db[0]), DualNum.of(db[1])),
			CN.make(DualNum.of(db[2], db[7]), DualNum.of(db[3], db[8])), DualNum.of(db[4]))
		for t in BeamGeom.hits(dbeam, BeamGeom.seg_from_dict(dual_ctx, sj), false):
			if fdt.has(t.v):
				assert_almost_eq(fdt[t.v], t.d, 1e-6 * maxf(1.0, absf(t.d)), "beam %d seg %d: dt" % [case["beam"], case["seg"]])
		compared += 1
	assert_gt(compared, 100)
