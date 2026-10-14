extends GutTest
## Stages 91-92 differential test: NHPBeam / NHPBeamCircle, NBeamGeom (+Seg),
## NBeamEffects, NExact and NFastSolver's raw kernels against their GDScript
## twins on the stage80 geometry fixture, the solver fixtures' surfaces and
## seeded random inputs, at F64 and 4 limbs (and the float64 walk's raw beams).

const Diff := preload("res://tests/native/diff_helpers.gd")
const GEOMETRY := "res://bench/fixtures/stage80_geometry.json"
const SCENE_FIXTURES := ["stage81_rules", "stage82_portals", "stage83_projective", "stage85_flags", "stage87_projective_plans"]
const LIMBS := 4

var _geo: Dictionary
var _scenes: Array = [] # [{"name", "surfaces"}]


func before_all() -> void:
	var f := FileAccess.open(GEOMETRY, FileAccess.READ)
	_geo = JSON.parse_string(f.get_as_text())
	for name in SCENE_FIXTURES:
		var g := FileAccess.open("res://bench/fixtures/%s.json" % name, FileAccess.READ)
		for fx in JSON.parse_string(g.get_as_text())["fixtures"]:
			_scenes.append({"name": "%s/%s" % [name, fx["name"]], "surfaces": fx["surfaces"]})


func _rng(seed: int) -> RandomNumberGenerator:
	var r := RandomNumberGenerator.new()
	r.seed = seed
	return r


static func _tiers() -> Array:
	return [[NumCtx.f64(), NNumCtx.f64()], [NumCtx.with_limbs(LIMBS), NNumCtx.with_limbs(LIMBS)]]


# ---------------------------------------------------------------- comparators

static func circle_diff(a, b, label: String) -> String:
	if a == null or b == null:
		return "" if a == null and b == null else "%s: %s vs %s" % [label, a, b]
	if a.id != b.id:
		return "%s.id: %d vs %d" % [label, a.id, b.id]
	var r := Diff.cn_diff(a.c, b.c, label + ".c")
	if r == "":
		r = Diff.num_diff(a.r, b.r, label + ".r")
	if r == "":
		r = Diff.num_diff(a.r2, b.r2, label + ".r2")
	return r


static func seg_diff(a, b, label: String) -> String:
	if a == null or b == null:
		return "" if a == null and b == null else "%s: %s vs %s" % [label, a, b]
	if a.id != b.id or a.kind != b.kind or a.full != b.full:
		return "%s: (id %d kind %d full %s) vs (id %d kind %d full %s)" % [label, a.id, a.kind, a.full, b.id, b.kind, b.full]
	var r := Diff.cn_diff(a.s, b.s, label + ".s")
	if r == "":
		r = Diff.cn_diff(a.e, b.e, label + ".e")
	if r == "":
		r = Diff.cn_diff(a.v, b.v, label + ".v")
	if r == "":
		r = circle_diff(a.circ, b.circ, label + ".circ")
	return r


static func nums_diff(a: Array, b: Array, label: String) -> String:
	if a.size() != b.size():
		return "%s: %d vs %d values" % [label, a.size(), b.size()]
	for i in a.size():
		var r := Diff.num_diff(a[i], b[i], "%s[%d]" % [label, i])
		if r != "":
			return r
	return ""


## Raw float64 arrays: values bit for bit. Derivative components (from index
## `deriv_from`: 2 for points / tangents, 5 for beams, odd indices for roots)
## compare up to the sign of zero: FastSolver's hand-written derivatives keep
## a -0.0 where dual arithmetic adds a +0.0 term (dtx * 1.0 vs dtx + tx * 0.0);
## derivatives only ever reach gains as norms, so the sign is unobservable.
static func packed_diff(a: PackedFloat64Array, b: PackedFloat64Array, label: String, deriv_from: int = -1, deriv_odd: bool = false) -> String:
	if a.size() != b.size():
		return "%s: %d vs %d values" % [label, a.size(), b.size()]
	if a.to_byte_array() != b.to_byte_array():
		for i in a.size():
			if Diff.same_float(a[i], b[i]):
				continue
			var is_deriv: bool = (deriv_from >= 0 and i >= deriv_from) or (deriv_odd and i % 2 == 1)
			if is_deriv and a[i] == 0.0 and b[i] == 0.0:
				continue
			return "%s[%d]: %s vs %s" % [label, i, Diff.hex(a[i]), Diff.hex(b[i])]
	return ""


# ---------------------------------------------------------------- fixtures

func _beam(ctx, bj: Dictionary):
	return HPBeam.make(ctx.cn(bj["p"][0], bj["p"][1]), ctx.cn(bj["tau"][0], bj["tau"][1]), ctx.num(bj["kappa"]))


func _nbeam(ctx: NNumCtx, bj: Dictionary) -> NHPBeam:
	return NHPBeam.make(ctx.cn(bj["p"][0], bj["p"][1]), ctx.cn(bj["tau"][0], bj["tau"][1]), ctx.num(bj["kappa"]))


func test_segments_and_hits_identical() -> void:
	var mismatches: Array = []
	var checks := 0
	for pair in _tiers():
		var ctx: NumCtx = pair[0]
		var nctx: NNumCtx = pair[1]
		var segs: Array = []
		var nsegs: Array = []
		for sj in _geo["segments"]:
			var sg := BeamGeom.seg_from_dict(ctx, sj)
			var ns := NBeamGeom.seg_from_dict(nctx, sj)
			var r := seg_diff(sg, ns, "%s seg %d" % [ctx.name(), sj["id"]])
			if r != "":
				mismatches.append(r)
			segs.append(sg)
			nsegs.append(ns)
			var w := Diff.num_diff(BeamGeom.winding(sg), NBeamGeom.winding(ns), "%s seg %d winding" % [ctx.name(), sj["id"]])
			if w != "":
				mismatches.append(w)
		var stride: int = 1 if ctx.is_f64() else 3
		for bi in range(0, _geo["beams"].size(), stride):
			var bj: Dictionary = _geo["beams"][bi]
			var b = _beam(ctx, bj)
			var nb := _nbeam(nctx, bj)
			var r := Diff.beam_diff(b, nb, "%s beam %d" % [ctx.name(), bi])
			if r == "":
				r = Diff.cn_diff(b.at_infinity(), nb.at_infinity(), "%s beam %d at_infinity" % [ctx.name(), bi])
			if r != "":
				mismatches.append(r)
				continue
			for si in range(0, segs.size(), stride):
				var label := "%s beam %d seg %d" % [ctx.name(), bi, segs[si].id]
				var roots: Array = BeamGeom.hits(b, segs[si], false)
				var nroots: Array = NBeamGeom.hits(nb, nsegs[si], false)
				r = nums_diff(roots, nroots, label + " hits")
				if r == "":
					var recs: Array = BeamGeom.hit_records(b, segs[si], false)
					var nrecs: Array = NBeamGeom.hit_records(nb, nsegs[si], false)
					if recs.size() != nrecs.size():
						r = "%s hit_records: %d vs %d" % [label, recs.size(), nrecs.size()]
					for k in mini(recs.size(), nrecs.size()):
						if r != "":
							break
						r = Diff.num_diff(recs[k]["t"], nrecs[k]["t"], label + " rec.t")
						if r == "":
							r = Diff.cn_diff(recs[k]["point"], nrecs[k]["point"], label + " rec.point")
						if r == "" and (recs[k]["at_endpoint"] != nrecs[k]["at_endpoint"] or recs[k]["on_segment"] != nrecs[k]["on_segment"]):
							r = "%s rec flags" % label
				for k in mini(roots.size(), nroots.size()):
					if r != "":
						break
					var pt = b.at(roots[k])
					var npt := nb.at(nroots[k])
					r = Diff.cn_diff(pt, npt, label + " at")
					if r == "":
						r = Diff.cn_diff(b.tangent_at(roots[k]), nb.tangent_at(nroots[k]), label + " tangent_at")
					if r == "":
						r = Diff.num_diff(b.param_of(pt), nb.param_of(npt), label + " param_of")
					if r == "":
						var sl := BeamGeom.side_left(segs[si], pt, b.tangent_at(roots[k]))
						var nsl := NBeamGeom.side_left(nsegs[si], npt, nb.tangent_at(nroots[k]))
						if sl != nsl or BeamGeom.on_segment(segs[si], pt) != NBeamGeom.on_segment(nsegs[si], npt) \
								or BeamGeom.endpoint_at(segs[si], pt) != NBeamGeom.endpoint_at(nsegs[si], npt):
							r = "%s decisions at root %d" % [label, k]
					if r == "":
						r = Diff.num_diff(BeamGeom.on_segment_value(segs[si], pt), NBeamGeom.on_segment_value(nsegs[si], npt), label + " on_segment_value")
					if r == "":
						r = Diff.cn_diff(BeamGeom._grad(segs[si], pt), NBeamGeom.grad(nsegs[si], npt), label + " grad")
				if r != "":
					mismatches.append(r)
				checks += 1
	assert_gt(checks, 200)
	assert_eq(mismatches.size(), 0, "%d mismatches\n%s" % [mismatches.size(), "\n".join(mismatches.slice(0, 20))])


func test_endpoints_blocking_and_provenance_identical() -> void:
	var mismatches: Array = []
	var rng := _rng(92)
	for pair in _tiers():
		var ctx: NumCtx = pair[0]
		var nctx: NNumCtx = pair[1]
		for sj in _geo["segments"]:
			var sg := BeamGeom.seg_from_dict(ctx, sj)
			var ns := NBeamGeom.seg_from_dict(nctx, sj)
			var label := "%s seg %d" % [ctx.name(), sj["id"]]
			for which in [1, 2]:
				var ti := BeamGeom.tangent_into(sg, which)
				var nti := NBeamGeom.tangent_into(ns, which)
				var r := Diff.cn_diff(ti, nti, "%s tangent_into %d" % [label, which])
				if r != "":
					mismatches.append(r)
				for i in 6:
					var ang := rng.randf_range(-PI, PI)
					var d = ctx.cn(cos(ang), sin(ang))
					var nd := nctx.cn(cos(ang), sin(ang))
					var bl: Array = BeamGeom.blocked_sides(sg, which, d)
					var nbl: Array = NBeamGeom.blocked_sides(ns, which, nd)
					if bl != nbl:
						mismatches.append("%s blocked_sides %d dir %s: %s vs %s" % [label, which, ang, bl, nbl])
				# exact endpoints and provenance (known point at an endpoint)
				var ep = sg.s if which == 1 else sg.e
				var nep = ns.s if which == 1 else ns.e
				if BeamGeom.endpoint_at(sg, ep) != NBeamGeom.endpoint_at(ns, nep) or not BeamGeom._eq(ep, ep) or not NBeamGeom.eq(nep, nep):
					mismatches.append("%s endpoint_at %d" % [label, which])
				var b = HPBeam.make(ctx.cn(ep.fx() - 50.0, ep.fy() + 3.0), ctx.cn(1.0, 0.0), ctx.num(0.0))
				var nb := NHPBeam.make(nctx.cn(ep.fx() - 50.0, ep.fy() + 3.0), nctx.cn(1.0, 0.0), nctx.num(0.0))
				var recs: Array = BeamGeom.hit_records(b, sg, false, [[ep, ctx.num(15.0)]])
				var nrecs: Array = NBeamGeom.hit_records(nb, ns, false, [[nep, nctx.num(15.0)]])
				if recs.size() != nrecs.size():
					mismatches.append("%s provenance records %d vs %d" % [label, recs.size(), nrecs.size()])
				for k in mini(recs.size(), nrecs.size()):
					var r2 := Diff.num_diff(recs[k]["t"], nrecs[k]["t"], label + " prov.t")
					if r2 == "":
						r2 = Diff.cn_diff(recs[k]["point"], nrecs[k]["point"], label + " prov.point")
					if r2 == "" and (recs[k]["at_endpoint"] != nrecs[k]["at_endpoint"] or recs[k]["on_segment"] != nrecs[k]["on_segment"]):
						r2 = "%s prov flags" % label
					if r2 != "":
						mismatches.append(r2)
			# random points: on_segment and circumcenter
			for i in 10:
				var x := rng.randf_range(0, 1920)
				var y := rng.randf_range(0, 1080)
				if BeamGeom.on_segment(sg, ctx.cn(x, y)) != NBeamGeom.on_segment(ns, nctx.cn(x, y)):
					mismatches.append("%s on_segment (%s, %s)" % [label, x, y])
		for i in 20:
			var pts: Array = []
			for k in 3:
				pts.append(Vector2(rng.randf_range(-500, 500), rng.randf_range(-500, 500)))
			var c = BeamGeom.circumcenter(ctx.cn(pts[0].x, pts[0].y), ctx.cn(pts[1].x, pts[1].y), ctx.cn(pts[2].x, pts[2].y))
			var nc := NBeamGeom.circumcenter(nctx.cn(pts[0].x, pts[0].y), nctx.cn(pts[1].x, pts[1].y), nctx.cn(pts[2].x, pts[2].y))
			var r := Diff.cn_diff(c, nc, "%s circumcenter %d" % [ctx.name(), i])
			if r != "":
				mismatches.append(r)
	assert_eq(mismatches.size(), 0, "%d mismatches\n%s" % [mismatches.size(), "\n".join(mismatches.slice(0, 20))])


func test_segment_from_game_segment_identical() -> void:
	var segs: Array = [
		Segment.from_coords(Vector2(100, 100), Vector2(400, 150), Vector2(250, 125)),
		Segment.from_coords(Vector2(400, 250), Vector2(300, 250), Vector2(350, 200)),
		Segment.from_coords(Vector2(0, 0), Vector2(0, 600), Vector2(0, 300)),
	]
	for pair in _tiers():
		var ctx: NumCtx = pair[0]
		var nctx: NNumCtx = pair[1]
		for i in segs.size():
			var r := seg_diff(BeamGeom.seg_from_segment(ctx, segs[i], i + 1), NBeamGeom.seg_from_segment(nctx, segs[i], i + 1), "%s segment %d" % [ctx.name(), i])
			assert_eq(r, "", r)


func test_beam_constructions_identical() -> void:
	var mismatches: Array = []
	var rng := _rng(93)
	for pair in _tiers():
		var ctx: NumCtx = pair[0]
		var nctx: NNumCtx = pair[1]
		var n: int = 60 if ctx.is_f64() else 20
		for i in n:
			var P := Vector2(rng.randf_range(-800, 800), rng.randf_range(-800, 800))
			var X := Vector2(rng.randf_range(-800, 800), rng.randf_range(-800, 800))
			var Q := Vector2(rng.randf_range(-800, 800), rng.randf_range(-800, 800))
			var label := "%s through %d" % [ctx.name(), i]
			var delta = ctx.cn(P.x - Q.x, P.y - Q.y)
			var ndelta := nctx.cn(P.x - Q.x, P.y - Q.y)
			var b = HPBeam.through(ctx.cn(P.x, P.y), ctx.cn(X.x, X.y), null if i % 3 == 0 else delta)
			var nb := NHPBeam.through(nctx.cn(P.x, P.y), nctx.cn(X.x, X.y), null if i % 3 == 0 else ndelta)
			var r := Diff.beam_diff(b, nb, label)
			if r != "":
				mismatches.append(r)
				continue
			var circ := HPBeam.Circle.make(7, ctx.cn(Q.x, Q.y), ctx.num(rng.randf_range(20, 300)))
			var ncirc := NHPBeamCircle.make(7, nctx.cn(Q.x, Q.y), nctx.num(circ.r.to_float()))
			r = circle_diff(circ, ncirc, label + " circle")
			if r == "":
				r = Diff.cn_diff(HPBeam.invert_point(ctx.cn(X.x, X.y), circ), NHPBeam.invert_point(nctx.cn(X.x, X.y), ncirc), label + " invert_point")
			if r == "":
				var roots: Array = b.hits(circ, false)
				var nroots: Array = nb.hits(ncirc, false)
				r = nums_diff(roots, nroots, label + " circle hits")
				for k in mini(roots.size(), nroots.size()):
					if r != "" or roots[k].is_inf():
						break
					r = Diff.beam_diff(b.invert_at(roots[k], circ), nb.invert_at(nroots[k], ncirc), label + " invert_at")
					if r == "":
						r = Diff.beam_diff(b.invert_at(roots[k], circ, b.at(roots[k])), nb.invert_at(nroots[k], ncirc, nb.at(nroots[k])), label + " invert_at exact")
			if r == "":
				r = Diff.num_diff(HPBeam.inf_like(ctx.num(1.0)), NHPBeam.inf_like(nctx.num(1.0)), label + " inf_like")
			if r != "":
				mismatches.append(r)
	assert_eq(mismatches.size(), 0, "%d mismatches\n%s" % [mismatches.size(), "\n".join(mismatches.slice(0, 20))])


func test_fast_raw_kernels_identical() -> void:
	var mismatches: Array = []
	var rng := _rng(94)
	for i in 300:
		var raw := FastSolver.seeded(FastSolver.beam(rng.randf_range(-500, 500), rng.randf_range(-500, 500), cos(i), sin(i),
			0.0 if i % 5 == 0 else rng.randf_range(-0.02, 0.02)))
		var nraw := NFastSolver.seeded(NFastSolver.beam(raw[0], raw[1], raw[2], raw[3], raw[4]))
		var label := "raw %d" % i
		var r := packed_diff(raw, nraw, label + " seeded")
		var t := rng.randf_range(-400, 400)
		var dt := rng.randf_range(-1, 1)
		if r == "":
			r = packed_diff(FastSolver.at(raw, t, dt), NFastSolver.at(nraw, t, dt), label + " at", 2)
		if r == "":
			r = packed_diff(FastSolver.at_view(raw, t * 1000.0), NFastSolver.at_view(nraw, t * 1000.0), label + " at_view", 2)
		if r == "":
			r = packed_diff(FastSolver.tangent_at(raw, t, dt), NFastSolver.tangent_at(nraw, t, dt), label + " tangent_at", 2)
		if r == "" and raw[4] != 0.0:
			r = packed_diff(FastSolver.at(raw, INF, 0.0), NFastSolver.at(nraw, INF, 0.0), label + " antipode", 2)
		var h := FastSolver.at(raw, t, 0.0)
		if r == "" and not Diff.same_float(FastSolver.param_of(raw, h[0], h[1]), NFastSolver.param_of(nraw, h[0], h[1])):
			r = "%s param_of" % label
		var sx := rng.randf_range(-600, 600)
		var sy := rng.randf_range(-600, 600)
		var ex := rng.randf_range(-600, 600)
		var ey := rng.randf_range(-600, 600)
		if r == "":
			r = packed_diff(FastSolver.line_hits(raw, sx, sy, ex, ey, false), NFastSolver.line_hits(nraw, sx, sy, ex, ey, false), label + " line_hits", -1, true)
		if r == "":
			r = packed_diff(FastSolver.line_hits(raw, raw[0], raw[1], ex, ey, true), NFastSolver.line_hits(nraw, raw[0], raw[1], ex, ey, true), label + " line_hits depart", -1, true)
		var cr := rng.randf_range(10, 400)
		if r == "":
			r = packed_diff(FastSolver.circle_hits(raw, sx, sy, cr * cr, false), NFastSolver.circle_hits(nraw, sx, sy, cr * cr, false), label + " circle_hits", -1, true)
		if r == "":
			r = packed_diff(FastSolver.reflect_line(raw, t, dt, sx, sy, ex, ey), NFastSolver.reflect_line(nraw, t, dt, sx, sy, ex, ey), label + " reflect_line", 5)
		if r == "":
			r = packed_diff(FastSolver.reflect_line(raw, t, dt, sx, sy, ex, ey, true, h[0], h[1]),
				NFastSolver.reflect_line(nraw, t, dt, sx, sy, ex, ey, true, h[0], h[1]), label + " reflect_line exact", 5)
		if r == "":
			r = packed_diff(FastSolver.invert_circle(raw, t, dt, sx, sy, cr, cr * cr), NFastSolver.invert_circle(nraw, t, dt, sx, sy, cr, cr * cr), label + " invert_circle", 5)
		if r != "":
			mismatches.append(r)
	assert_eq(mismatches.size(), 0, "%d mismatches\n%s" % [mismatches.size(), "\n".join(mismatches.slice(0, 20))])


func test_fast_surface_kernels_identical() -> void:
	var mismatches: Array = []
	var rng := _rng(95)
	var checks := 0
	for scn in _scenes:
		var sc := FastSolver.Scene.from_defs(scn["surfaces"])
		var nsc := NFastSolverScene.from_defs(scn["surfaces"])
		for si in sc.scene.surfaces.size():
			var s: BeamScene.Surf = sc.scene.surfaces[si]
			var ns: NBeamSceneSurf = nsc.scene.surfaces[si]
			var label := "%s surf %d" % [scn["name"], s.id]
			for i in 3:
				var raw := FastSolver.seeded(FastSolver.beam(rng.randf_range(-200, 1200), rng.randf_range(-600, 600), cos(i + si), sin(i + si),
					0.0 if i == 0 else rng.randf_range(-0.01, 0.01)))
				var hs := FastSolver.surf_hits(raw, s, false)
				var r := packed_diff(hs, NFastSolver.surf_hits(raw, ns, false), label + " surf_hits", -1, true)
				var j := 0
				while r == "" and j < hs.size():
					var t: float = hs[j]
					var dt: float = hs[j + 1]
					j += 2
					if is_inf(t):
						continue
					var h := FastSolver.at(raw, t, 0.0)
					r = packed_diff(FastSolver.apply_surf(raw, t, dt, s), NFastSolver.apply_surf(raw, t, dt, ns), label + " apply_surf", 5)
					if r == "":
						r = packed_diff(FastSolver.apply_surf(raw, t, dt, s, true, h[0], h[1]), NFastSolver.apply_surf(raw, t, dt, ns, true, h[0], h[1]), label + " apply_surf exact", 5)
					if r == "" and s.portal:
						r = packed_diff(FastSolver.portal(raw, t, dt, s), NFastSolver.portal(raw, t, dt, ns), label + " portal", 5)
					for left in [true, false]:
						if r == "" and s.effect(left) in BeamSolver.PROJECTIVE:
							r = packed_diff(FastSolver.project(raw, t, dt, s, left), NFastSolver.project(raw, t, dt, ns, left), label + " project", 5)
					var g := FastSolver.tangent_at(raw, t, 0.0)
					if r == "":
						if FastSolver.on_segment(s, h[0], h[1]) != NFastSolver.on_segment(ns, h[0], h[1]) \
								or FastSolver.side_left(s, h[0], h[1], g[0], g[1]) != NFastSolver.side_left(ns, h[0], h[1], g[0], g[1]) \
								or FastSolver.endpoint_at(s, h[0], h[1]) != NFastSolver.endpoint_at(ns, h[0], h[1]):
							r = "%s decisions" % label
					if r == "":
						for which in [1, 2]:
							if FastSolver.blocked_sides(s, which, g[0], g[1]) != NFastSolver.blocked_sides(ns, which, g[0], g[1]):
								r = "%s blocked_sides %d" % [label, which]
					checks += 1
				if r != "":
					mismatches.append(r)
	assert_gt(checks, 100)
	assert_eq(mismatches.size(), 0, "%d mismatches\n%s" % [mismatches.size(), "\n".join(mismatches.slice(0, 20))])


func test_effect_maps_identical() -> void:
	var mismatches: Array = []
	var rng := _rng(96)
	var checks := 0
	for pair in _tiers():
		var ctx: NumCtx = pair[0]
		var nctx: NNumCtx = pair[1]
		var stride: int = 1 if ctx.is_f64() else 4
		for sci in range(0, _scenes.size(), stride):
			var scn: Dictionary = _scenes[sci]
			var sc := BeamScene.from_defs(ctx, scn["surfaces"])
			var nsc := NBeamScene.from_defs(nctx, scn["surfaces"])
			for si in sc.surfaces.size():
				var s: BeamScene.Surf = sc.surfaces[si]
				var ns: NBeamSceneSurf = nsc.surfaces[si]
				var label := "%s %s surf %d" % [ctx.name(), scn["name"], s.id]
				var r := seg_diff(s.seg, ns.seg, label + " seg")
				if r == "":
					r = Diff.cn_diff(BeamEffects.u2(s.seg), NBeamEffects.u2(ns.seg), label + " u2") if s.line else ""
				# map_point: random point, null (infinity), the center
				var X := Vector2(rng.randf_range(-300, 1300), rng.randf_range(-500, 500))
				if r == "":
					for inverse in [false, true]:
						r = Diff.cn_diff(BeamEffects.map_point(ctx.cn(X.x, X.y), s, inverse), NBeamEffects.map_point(nctx.cn(X.x, X.y), ns, inverse), label + " map_point")
						if r == "":
							r = Diff.cn_diff(BeamEffects.map_point(null, s, inverse), NBeamEffects.map_point(null, ns, inverse), label + " map_point(inf)")
						if r != "":
							break
				if r == "" and not s.line:
					r = Diff.cn_diff(BeamEffects.map_point(s.seg.c(), s), NBeamEffects.map_point(ns.seg.c(), ns), label + " map_point(center)")
				if r == "" and s.portal:
					r = Diff.cn_diff(BeamEffects.portal_point(ctx.cn(X.x, X.y), s, true), NBeamEffects.portal_point(nctx.cn(X.x, X.y), ns, true), label + " portal_point")
				# map_pole on a random pole triple, with and without Q
				if r == "":
					var Q := Vector2(rng.randf_range(-300, 1300), rng.randf_range(-500, 500))
					var parent: Array = [ctx.cn(X.x, X.y), ctx.cn(Q.x, Q.y), ctx.cn(X.x - Q.x, X.y - Q.y)]
					var nparent: Array = [nctx.cn(X.x, X.y), nctx.cn(Q.x, Q.y), nctx.cn(X.x - Q.x, X.y - Q.y)]
					for variant in [[parent, nparent], [[parent[0], null, null], [nparent[0], null, null]]]:
						var pa: Array = BeamEffects.map_pole(variant[0], s)
						var pb: Array = NBeamEffects.map_pole(variant[1], ns)
						for k in 3:
							if r == "":
								r = Diff.cn_diff(pa[k], pb[k], "%s map_pole[%d]" % [label, k])
				# beams hitting the surface: apply / portal / project at each root
				for i in 3:
					if r != "":
						break
					var ang := rng.randf_range(-PI, PI)
					var kappa: float = 0.0 if i == 0 else rng.randf_range(-0.01, 0.01)
					var P := Vector2(rng.randf_range(-200, 1200), rng.randf_range(-600, 600))
					var b = HPBeam.make(ctx.cn(P.x, P.y), ctx.cn(cos(ang), sin(ang)), ctx.num(kappa))
					var nb := NHPBeam.make(nctx.cn(P.x, P.y), nctx.cn(cos(ang), sin(ang)), nctx.num(kappa))
					var roots: Array = BeamGeom.hits(b, s.seg, false)
					var nroots: Array = NBeamGeom.hits(nb, ns.seg, false)
					r = nums_diff(roots, nroots, label + " hits")
					for k in mini(roots.size(), nroots.size()):
						if r != "" or roots[k].is_inf():
							break
						var t = roots[k]
						var nt = nroots[k]
						for left in [true, false]:
							var etype: String = s.effect(left)
							if etype in BeamSolver.TRANSFORMATIVE:
								r = Diff.beam_diff(BeamEffects.apply(b, t, s, etype), NBeamEffects.apply(nb, nt, ns, etype), label + " apply")
								if r == "":
									r = Diff.beam_diff(BeamEffects.apply(b, t, s, etype, b.at(t)), NBeamEffects.apply(nb, nt, ns, etype, nb.at(nt)), label + " apply exact")
							elif etype in BeamSolver.PROJECTIVE:
								r = Diff.beam_diff(BeamEffects.project(b, t, s, left), NBeamEffects.project(nb, nt, ns, left), label + " project")
								if r == "":
									var H = b.at(t)
									var nH := nb.at(nt)
									r = Diff.cn_diff(BeamEffects.projective_exit(s, left, H), NBeamEffects.projective_exit(ns, left, nH), label + " projective_exit")
								if r == "":
									var ca: Array = BeamEffects.back_propagate_candidates(s, left, ctx.cn(X.x, X.y))
									var cb: Array = NBeamEffects.back_propagate_candidates(ns, left, nctx.cn(X.x, X.y))
									if ca.size() != cb.size():
										r = "%s candidates %d vs %d" % [label, ca.size(), cb.size()]
									for q in mini(ca.size(), cb.size()):
										if r == "":
											r = Diff.cn_diff(ca[q][0], cb[q][0], label + " candidate")
										if r == "" and ca[q][1] != cb[q][1]:
											r = "%s candidate direct flag" % label
									if r == "":
										for click in [null, Vector2(rng.randf_range(0, 1000), rng.randf_range(-400, 400))]:
											r = Diff.cn_diff(BeamEffects.back_propagate(s, left, ctx.cn(X.x, X.y), false, click),
												NBeamEffects.back_propagate(ns, left, nctx.cn(X.x, X.y), false, click), label + " back_propagate")
											if r != "":
												break
							if r != "":
								break
						checks += 1
				if r != "":
					mismatches.append(r)
	assert_gt(checks, 100)
	assert_eq(mismatches.size(), 0, "%d mismatches\n%s" % [mismatches.size(), "\n".join(mismatches.slice(0, 20))])


func test_exact_kernels_identical() -> void:
	var mismatches: Array = []
	var rng := _rng(91)
	assert_true(Diff.same_float(Exact.U, NExact.U()), "Exact.U bits: %s vs %s (Godot parses 1.1102230246251565e-16 two ulps low; B12)" % [Diff.hex(Exact.U), Diff.hex(NExact.U())])
	for i in 150:
		var vals: Array = []
		for k in 10:
			vals.append(rng.randf_range(-2000, 2000) * (1.0 if k % 3 else 1e-7))
		var label := "exact %d" % i
		var a := Exact.of(vals[0])
		var b := Exact.of(vals[1])
		var na := NExact.of(vals[0])
		var nb := NExact.of(vals[1])
		var r := Diff.num_diff(a, na, label + " of")
		if r == "":
			r = Diff.num_diff(Exact.mul(a, b), NExact.mul(na, nb), label + " mul")
		if r == "":
			r = Diff.num_diff(Exact.add(a, b), NExact.add(na, nb), label + " add")
		if r == "":
			r = Diff.num_diff(Exact.sub(a, b), NExact.sub(na, nb), label + " sub")
		var x := [vals[2], absf(vals[3]) * 1e-12]
		var y := [vals[4], absf(vals[5]) * 1e-12]
		if r == "":
			r = Diff.floats_diff(Exact._emul(x, y), NExact.emul(x, y), label + " emul")
		if r == "":
			r = Diff.floats_diff(Exact._eadd(x, y), NExact.eadd(x, y), label + " eadd")
		if r == "":
			r = Diff.floats_diff(Exact._esub(x, y), NExact.esub(x, y), label + " esub")
		# predicates on near-degenerate inputs: collinear / cocircular points perturbed by 0 or 1 ulp
		var ax: float = vals[0]
		var ay: float = vals[1]
		var bx: float = vals[2]
		var by: float = vals[3]
		var cx: float = ax + 2.0 * (bx - ax)
		var cy: float = ay + 2.0 * (by - ay) + (0.0 if i % 2 == 0 else 1e-9)
		if r == "" and Exact.orient_zero(ax, ay, bx, by, cx, cy) != NExact.orient_zero(ax, ay, bx, by, cx, cy):
			r = "%s orient_zero" % label
		var rr := absf(vals[6]) + 1.0
		var kx: float = ax + rr
		var ky: float = ay + (0.0 if i % 3 == 0 else 1e-9)
		if r == "" and Exact.on_circle_zero(ax, ay, rr, kx, ky) != NExact.on_circle_zero(ax, ay, rr, kx, ky):
			r = "%s on_circle_zero" % label
		if r == "" and Exact.incircle_zero(ax, ay, bx, by, cx, cy + 5.0, kx, ky) != NExact.incircle_zero(ax, ay, bx, by, cx, cy + 5.0, kx, ky):
			r = "%s incircle_zero" % label
		# homogeneous constructions
		if r == "":
			r = nums_diff(Exact.line_from_dir(ax, ay, bx, by), NExact.line_from_dir(ax, ay, bx, by), label + " line_from_dir")
		var L: Array = Exact.line_through(ax, ay, bx, by)
		var nL: Array = NExact.line_through(ax, ay, bx, by)
		if r == "":
			r = nums_diff(L, nL, label + " line_through")
		var X: Array = [Exact.of(cx), Exact.of(cy), Exact.of(1.0)]
		var nX: Array = [NExact.of(cx), NExact.of(cy), NExact.of(1.0)]
		if r == "":
			r = nums_diff(Exact.reflect_point_h(X, ax, ay, bx, by), NExact.reflect_point_h(nX, ax, ay, bx, by), label + " reflect_point_h")
		if r == "":
			r = nums_diff(Exact.reflect_line(L, bx, by, cx, cy), NExact.reflect_line(nL, bx, by, cx, cy), label + " reflect_line")
		if r == "":
			r = nums_diff(Exact.line_through_h(ax, ay, X), NExact.line_through_h(ax, ay, nX), label + " line_through_h")
		if r == "":
			r = nums_diff(Exact.circumcenter_h(ax, ay, bx, by, cx, cy + 5.0), NExact.circumcenter_h(ax, ay, bx, by, cx, cy + 5.0), label + " circumcenter_h")
		if r == "" and Exact.on_line_h(L, X) != NExact.on_line_h(nL, nX):
			r = "%s on_line_h" % label
		var C: Array = [Exact.of(ax), Exact.of(ay), Exact.of(1.0)]
		var nC: Array = [NExact.of(ax), NExact.of(ay), NExact.of(1.0)]
		var rw2 := Exact.mul(Exact.of(rr), Exact.of(rr))
		var nrw2 := NExact.mul(NExact.of(rr), NExact.of(rr))
		if r == "" and Exact.tangent_zero(L, C, rw2) != NExact.tangent_zero(nL, nC, nrw2):
			r = "%s tangent_zero" % label
		if r == "":
			var ia = Exact.invert_point_h(X, C, rw2)
			var ib = NExact.invert_point_h(nX, nC, nrw2)
			if (ia == null) != (ib == null):
				r = "%s invert_point_h null" % label
			elif ia != null:
				r = nums_diff(ia, ib, label + " invert_point_h")
		if r != "":
			mismatches.append(r)
	assert_eq(mismatches.size(), 0, "%d mismatches\n%s" % [mismatches.size(), "\n".join(mismatches.slice(0, 20))])
