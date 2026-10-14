extends GutTest

# Stage 81: beam maps of reflections. Expected values are closed forms: the
# reflection of a point across a line, S + u²·conj(z − S), is independent of
# the beam map under test. Arc reflection is specified as inversion in the
# carrier (GAME_SPEC: reflection on a circle = inversion).

func _rng() -> RandomNumberGenerator:
	var r := RandomNumberGenerator.new()
	r.seed = 81
	return r

# closed-form reflection of point (x, y) across the line through s -> e (float64)
static func _mirror(x: float, y: float, sx: float, sy: float, ex: float, ey: float) -> PackedFloat64Array:
	var dx := ex - sx
	var dy := ey - sy
	var n := dx * dx + dy * dy
	# u² = (dx + i dy)² / n
	var u2r := (dx * dx - dy * dy) / n
	var u2i := (2.0 * dx * dy) / n
	var wx := x - sx
	var wy := -(y - sy)            # conj(z - S)
	return PackedFloat64Array([sx + u2r * wx - u2i * wy, sy + u2r * wy + u2i * wx])

func _line_seg(ctx: NumCtx, sid: int, s: Vector2, e: Vector2) -> BeamGeomSeg:
	return BeamGeom.seg_from_dict(ctx, {"kind": "line", "id": sid, "start": [s.x, s.y], "end": [e.x, e.y],
		"via": [(s.x + e.x) / 2.0, (s.y + e.y) / 2.0]})

# ---------------------------------------------------------------- line reflection

func test_line_reflection_beam_map():
	var ctx := NumCtx.f64()
	var rng := _rng()
	var checked := 0
	for i in 200:
		var ang := rng.randf_range(-PI, PI)
		var kappa: float = 0.0 if i % 4 == 0 else rng.randf_range(-0.02, 0.02)
		var b := HPBeam.make(ctx.cn(rng.randf_range(-300, 300), rng.randf_range(-300, 300)),
			ctx.cn(cos(ang), sin(ang)), ctx.num(kappa))
		# a line through a point of the beam, in a random direction
		var th := rng.randf_range(5.0, 120.0)
		var h := b.at(ctx.num(th))
		var la := rng.randf_range(-PI, PI)
		var s := Vector2(h.fx() - 50.0 * cos(la), h.fy() - 50.0 * sin(la))
		var e := Vector2(h.fx() + 70.0 * cos(la), h.fy() + 70.0 * sin(la))
		var seg := _line_seg(ctx, 1, s, e)
		var roots := BeamGeom.hits(b, seg, false)
		if roots.is_empty():
			continue
		var t = roots[0]
		for r in roots:
			if absf(r.to_float() - th) < absf(t.to_float() - th):
				t = r
		var out: HPBeam = BeamEffects.reflect_line(b, t, seg)
		assert_not_null(out, "reflect_line returns a beam")
		if out == null:
			return
		# the outgoing beam starts at the hit
		assert_lt(Vector2(out.p.fx(), out.p.fy()).distance_to(Vector2(b.at(t).fx(), b.at(t).fy())), 1e-9)
		# it contains the mirror images of 3 points of the incoming beam, and
		# the images of the incoming continuation lie ahead of the departure
		for dt in [3.0, 17.0, 41.0]:
			var z := b.at(t.add(ctx.num(dt)))
			var m := _mirror(z.fx(), z.fy(), s.x, s.y, e.x, e.y)
			var mz := ctx.cn(m[0], m[1])
			var u = out.param_of(mz)
			var back := out.at(u)
			assert_lt(Vector2(back.fx(), back.fy()).distance_to(Vector2(m[0], m[1])), 1e-6,
				"beam %d: mirror image of incoming point (dt=%.0f) lies on the outgoing beam" % [i, dt])
			assert_gt(u.to_float(), 0.0, "beam %d: continuation maps ahead of the departure" % i)
		assert_eq(out.kappa.to_float(), -kappa, "beam %d: κ' = −κ exactly" % i)
		checked += 1
	assert_gt(checked, 150)

# ---------------------------------------------------------------- arc reflection

func test_arc_reflection_equals_inversion():
	var ctx := NumCtx.f64()
	var refl := BeamScene.from_defs(ctx, [{"id": 1, "line": false, "start": [700.0, 0.0], "end": [300.0, 0.0],
		"via": [500.0, -200.0], "full": false, "left": {"type": "reflection"}, "right": {"type": "reflection"}}])
	assert_not_null(refl, "BeamScene.from_defs")
	if refl == null:
		return
	var surf = refl.by_id[1]
	var circ := HPBeamCircle.make(1, ctx.cn(500.0, 0.0), ctx.num(200.0))
	var rng := _rng()
	for i in 50:
		var ang := rng.randf_range(-PI, PI)
		var b := HPBeam.make(ctx.cn(rng.randf_range(-100, 100), rng.randf_range(-400, 400)), ctx.cn(cos(ang), sin(ang)),
			ctx.num(0.0 if i % 2 == 0 else rng.randf_range(-0.01, 0.01)))
		var roots := BeamGeom.hits(b, surf.seg, false)
		for t in roots:
			var a: HPBeam = BeamEffects.apply(b, t, surf, "reflection")
			var x: HPBeam = b.invert_at(t, circ)
			assert_eq([a.p.fx(), a.p.fy(), a.tau.fx(), a.tau.fy(), a.kappa.to_float()],
				[x.p.fx(), x.p.fy(), x.tau.fx(), x.tau.fy(), x.kappa.to_float()], "beam %d: reflection on an arc = inversion" % i)

# ---------------------------------------------------------------- FastSolver twin

func test_fast_twin_reflection():
	var gen := NumCtx.f64()
	var rng := _rng()
	var compared := 0
	for i in 100:
		var ang := rng.randf_range(-PI, PI)
		var px := rng.randf_range(-300, 300)
		var py := rng.randf_range(-300, 300)
		var k: float = 0.0 if i % 3 == 0 else rng.randf_range(-0.02, 0.02)
		var s := Vector2(rng.randf_range(-400, 400), rng.randf_range(-400, 400))
		var e := Vector2(rng.randf_range(-400, 400), rng.randf_range(-400, 400))
		var seg := _line_seg(gen, 1, s, e)
		var b := HPBeam.make(gen.cn(px, py), gen.cn(cos(ang), sin(ang)), gen.num(k))
		var raw := FastSolver.seeded(FastSolver.beam(px, py, cos(ang), sin(ang), k))
		var fr := FastSolver.line_hits(raw, s.x, s.y, e.x, e.y, false)
		var gr := BeamGeom.hits(b, seg, false)
		if fr.size() == 0:
			continue
		for j in gr.size():
			var t = gr[j]
			# matching raw root (bit-identical values, Stage 80)
			var dt := 0.0
			var q := 0
			while q < fr.size():
				if fr[q] == t.to_float():
					dt = fr[q + 1]
				q += 2
			var g: HPBeam = BeamEffects.reflect_line(b, t, seg)
			var f: PackedFloat64Array = FastSolver.reflect_line(raw, t.to_float(), dt, s.x, s.y, e.x, e.y)
			assert_eq(f.size(), 10, "raw beam layout")
			if f.size() != 10:
				return
			assert_eq([f[0], f[1], f[2], f[3], f[4]], [g.p.fx(), g.p.fy(), g.tau.fx(), g.tau.fy(), g.kappa.to_float()],
				"beam %d: bit-identical reflected beam" % i)
			# dual parts vs the generic DualNum map
			var dctx := NumCtx.dual()
			var db := HPBeam.make(CN.make(DualNum.of(raw[0]), DualNum.of(raw[1])),
				CN.make(DualNum.of(raw[2], raw[7]), DualNum.of(raw[3], raw[8])), DualNum.of(raw[4]))
			var dseg := _line_seg(dctx, 1, s, e)
			for dtn in BeamGeom.hits(db, dseg, false):
				if dtn.v == t.to_float():
					var dg: HPBeam = BeamEffects.reflect_line(db, dtn, dseg)
					var got := [f[5], f[6], f[7], f[8], f[9]]
					var want := [dg.p.re.d, dg.p.im.d, dg.tau.re.d, dg.tau.im.d, dg.kappa.d]
					for c in 5:
						assert_almost_eq(got[c], want[c], 1e-6 * maxf(1.0, absf(want[c])), "beam %d: dual part %d" % [i, c])
			compared += 1
	assert_gt(compared, 60)
