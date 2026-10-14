extends GutTest
## Stage 97 differential test: the native visual / testing kernels against the
## GDScript originals (Gd* copies): GeneralizedCircle, Segment (carriers,
## sides, the click distance), VisualConverter (display paths, arc params,
## clipping), StepTreeMerge, ArrowAdvance and SurfaceGeom, bit for bit on the
## sweep scenes' traces and on seeded random inputs.

const Diff := preload("res://tests/native/diff_helpers.gd")

var _mismatches: Array = []
var _checks := 0


func _rng(seed: int) -> RandomNumberGenerator:
	var r := RandomNumberGenerator.new()
	r.seed = seed
	return r


func _report(what: String) -> void:
	assert_gt(_checks, 0, "%s: checks" % what)
	assert_eq(_mismatches.size(), 0, "%s: %d of %d checks differ\n%s" % [what, _mismatches.size(), _checks, "\n".join(_mismatches.slice(0, 20))])
	_mismatches.clear()
	_checks = 0


func _note(r: String) -> void:
	_checks += 1
	if r != "":
		_mismatches.append(r)


static func carrier_diff(a, b, label: String) -> String:
	for f in ["a", "b", "c", "d"]:
		if not Diff.same_float(a.get(f), b.get(f)):
			return "%s.%s: %s vs %s" % [label, f, Diff.hex(a.get(f)), Diff.hex(b.get(f))]
	if a.is_line() != b.is_line():
		return "%s.is_line" % label
	if not a.is_line():
		if not Diff.same_vec(a.center(), b.center()) or not Diff.same_float(a.radius(), b.radius()):
			return "%s.center/radius" % label
	return ""


static func arc_params_diff(a: Dictionary, b: Dictionary, label: String) -> String:
	if not Diff.same_vec(a["center"], b["center"]):
		return "%s.center: %s vs %s" % [label, a["center"], b["center"]]
	for k in ["radius", "start_angle", "end_angle", "span"]:
		if not Diff.same_float(a[k], b[k]):
			return "%s.%s: %s vs %s" % [label, k, Diff.hex(a[k]), Diff.hex(b[k])]
	if a["clockwise"] != b["clockwise"] or a["point_count"] != b["point_count"]:
		return "%s.clockwise/point_count" % label
	return ""


## Two steps built by the display kernels: geometry bits, meta, the same hit / ray objects.
static func display_step_diff(a, b, label: String) -> String:
	if a == null or b == null:
		return "" if a == null and b == null else "%s: %s vs %s" % [label, a, b]
	for f in ["start", "end", "via"]:
		if not Diff.same_vec(a.get(f), b.get(f)):
			return "%s.%s: %s vs %s" % [label, f, a.get(f), b.get(f)]
	for f in ["frame_id", "type", "surface_id", "hit_side", "hit_on_segment", "after_portal", "ghost", "is_arc_step", "precision_bits"]:
		if a.get(f) != b.get(f):
			return "%s.%s: %s vs %s" % [label, f, a.get(f), b.get(f)]
	if not Diff.same_float(a.est_error, b.est_error):
		return "%s.est_error" % label
	if a.hit != b.hit:
		return "%s.hit: different objects" % label
	if a.ray != b.ray:
		return "%s.ray: different objects" % label
	return ""


static func steps_diff(a: Array, b: Array, label: String) -> String:
	if a.size() != b.size():
		return "%s: %d vs %d steps" % [label, a.size(), b.size()]
	for i in a.size():
		var r := display_step_diff(a[i], b[i], "%s[%d]" % [label, i])
		if r != "":
			return r
	return ""


static func display_path_diff(a, b, label: String) -> String:
	var r := steps_diff(a.steps, b.steps, label + ".steps")
	if r != "":
		return r
	if a.cursor_index != b.cursor_index or a.hit_count != b.hit_count:
		return "%s: cursor_index/hit_count (%d, %d) vs (%d, %d)" % [label, a.cursor_index, a.hit_count, b.cursor_index, b.hit_count]
	if a.targets_hit != b.targets_hit:
		return "%s.targets_hit" % label
	return ""


# ---------------------------------------------------------------- carriers and segments

func test_carriers_and_segments_random() -> void:
	var rng := _rng(97)
	for i in 400:
		var s := Vector2(rng.randf_range(-1000, 2500), rng.randf_range(-1000, 2000))
		var e := Vector2(rng.randf_range(-1000, 2500), rng.randf_range(-1000, 2000))
		var v: Vector2
		match i % 4:
			0:
				v = (s + e) / 2.0 # a line
			1:
				v = Vector2(INF, INF) # a full line
			_:
				v = Vector2(rng.randf_range(-1000, 2500), rng.randf_range(-1000, 2000))
		var label := "segment %d" % i
		var gs := GdSegment.from_coords(s, e, v)
		var ns := Segment.from_coords(s, e, v)
		_note(carrier_diff(gs.get_carrier(), ns.get_carrier(), label + ".carrier"))
		if gs.is_line() != ns.is_line():
			_note("%s.is_line" % label)
		_note("" if Diff.same_float(gs._compute_winding(), ns.winding()) else "%s.winding: %s vs %s" % [label, Diff.hex(gs._compute_winding()), Diff.hex(ns.winding())])
		for k in 6:
			var p := Vector2(rng.randf_range(-1200, 2700), rng.randf_range(-1200, 2200))
			if k == 5:
				p = s # an endpoint
			if gs.determine_side(p) != ns.determine_side(p):
				_note("%s.determine_side(%s): %s vs %s" % [label, p, gs.determine_side(p), ns.determine_side(p)])
			else:
				_checks += 1
			var dg: float = GdClickGeom.distance_to_segment(p, gs)
			var dn: float = ns.distance_to(p)
			_note("" if Diff.same_float(dg, dn) else "%s.distance_to(%s): %s vs %s" % [label, p, Diff.hex(dg), Diff.hex(dn)])
		# carriers from circles and lines, full segments
		var c := Vector2(rng.randf_range(0, 1920), rng.randf_range(0, 1080))
		var r := rng.randf_range(5, 900)
		var gc := GdGeneralizedCircle.from_circle(c, r)
		var nc := GeneralizedCircle.from_circle(c, r)
		_note(carrier_diff(gc, nc, label + ".from_circle"))
		_note("" if Diff.same_float(gc.evaluate(s), nc.evaluate(s)) else "%s.evaluate" % label)
		var gl := GdGeneralizedCircle.from_line(rng.randf_range(-3, 3), rng.randf_range(-3, 3), rng.randf_range(-2000, 2000))
		var nl := GeneralizedCircle.from_line(gl.b, gl.c, gl.d)
		_note(carrier_diff(gl, nl, label + ".from_line"))
		for pair in [[gc, nc], [gl, nl]]:
			var gf := GdSegment.full_from_carrier(pair[0])
			var nf := Segment.full_from_carrier(pair[1])
			var rr := ""
			for f in ["start", "end", "via"]:
				if not Diff.same_vec(gf.get(f).coords, nf.get(f).coords):
					rr = "%s.full_from_carrier.%s: %s vs %s" % [label, f, gf.get(f).coords, nf.get(f).coords]
			if gf.full != nf.full or nf.get_carrier() != pair[1]:
				rr = "%s.full_from_carrier: full / carrier identity" % label
			_note(rr)
			var q := Vector2(rng.randf_range(-500, 2400), rng.randf_range(-500, 1600))
			_note("" if Diff.same_float(GdClickGeom.distance_to_segment(q, gf), nf.distance_to(q)) else "%s.full distance" % label)
			if gf.determine_side(q) != nf.determine_side(q):
				_note("%s.full determine_side" % label)
	_report("carriers and segments")


func test_arc_params_projection_and_clipping_random() -> void:
	var rng := _rng(98)
	var bounds := [VisualConverter.DEFAULT_BOUNDS(), Rect2(560, 240, 800, 600), Rect2(-100, -50, 300, 200)]
	for i in 500:
		var s := Vector2(rng.randf_range(-500, 2400), rng.randf_range(-500, 1600))
		var e := Vector2(rng.randf_range(-500, 2400), rng.randf_range(-500, 1600))
		var v := Vector2(rng.randf_range(-500, 2400), rng.randf_range(-500, 1600))
		if i % 5 == 0:
			v = (s + e) / 2.0
		if i % 7 == 0:
			e = Vector2(INF, INF)
		var label := "arc %d" % i
		if GdVisualConverter.is_arc(s, v, e) != VisualConverter.is_arc(s, v, e):
			_note("%s.is_arc" % label)
		else:
			_checks += 1
		if not is_inf(e.x):
			_note(arc_params_diff(GdVisualConverter.arc_params(s, v, e), VisualConverter.arc_params(s, v, e), label + ".arc_params"))
			var gcar = GdSegment.from_coords(s, e, v).get_carrier()
			var ncar = Segment.from_coords(s, e, v).get_carrier()
			var p := Vector2(rng.randf_range(-500, 2400), rng.randf_range(-500, 1600))
			if i % 11 == 0 and not gcar.is_line():
				p = gcar.center()
			_note("" if Diff.same_vec(GdVisualConverter.project_onto_carrier(p, gcar), VisualConverter.project_onto_carrier(p, ncar)) else "%s.project_onto_carrier" % label)
		var d := Vector2(rng.randf_range(-1, 1), rng.randf_range(-1, 1))
		if i % 9 == 0:
			d = Vector2.ZERO
		for b in bounds:
			_note("" if Diff.same_vec(GdVisualConverter._clip_to_bounds(s, d, b), VisualConverter.clip_to_bounds(s, d, b)) else "%s.clip_to_bounds %s" % [label, b])
	_report("arc params, projection, clipping")


func test_surface_samples_random() -> void:
	var rng := _rng(99)
	for i in 300:
		var a := Vector2(rng.randf_range(-500, 2400), rng.randf_range(-500, 1600))
		var b := Vector2(rng.randf_range(-500, 2400), rng.randf_range(-500, 1600))
		var t := rng.randf()
		var label := "sample %d" % i
		var gl: Dictionary = GdSurfaceGeom.line_sample(a, b, t)
		var nl: Dictionary = SurfaceGeom.line_sample(a, b, t)
		_note("" if Diff.same_vec(gl["position"], nl["position"]) and Diff.same_vec(gl["normal"], nl["normal"]) else "%s.line_sample" % label)
		var ga: Dictionary = GdSurfaceGeom.arc_sample(a, rng.randf_range(1, 800), rng.randf_range(-PI, PI), rng.randf_range(0, TAU), i % 2 == 0, t)
		var na: Dictionary = SurfaceGeom.arc_sample(a, ga["position"].distance_to(a), 0.0, 0.0, false, 0.0) # placeholder, replaced below
		var radius := rng.randf_range(1, 800)
		var sa := rng.randf_range(-PI, PI)
		var span := rng.randf_range(0, TAU)
		ga = GdSurfaceGeom.arc_sample(a, radius, sa, span, i % 2 == 0, t)
		na = SurfaceGeom.arc_sample(a, radius, sa, span, i % 2 == 0, t)
		_note("" if Diff.same_vec(ga["position"], na["position"]) and Diff.same_vec(ga["outward"], na["outward"]) else "%s.arc_sample" % label)
		var gc: PackedVector2Array = GdSurfaceGeom.chevron_vertices(a, b - a, rng.randf_range(2, 20))
		var nc: PackedVector2Array = SurfaceGeom.chevron_vertices(a, b - a, gc[0].distance_to(gc[1]) if false else 8.0)
		var size := rng.randf_range(2, 20)
		gc = GdSurfaceGeom.chevron_vertices(a, b - a, size)
		nc = SurfaceGeom.chevron_vertices(a, b - a, size)
		_note("" if gc.to_byte_array() == nc.to_byte_array() else "%s.chevron_vertices" % label)
	_report("surface samples")


# ---------------------------------------------------------------- the sweep scenes' traces

func _instantiate(path: String) -> Node:
	Surface.reset_id_counter()
	var scene: Node = load(path).instantiate()
	scene.gravity = Vector2.ZERO
	add_child_autofree(scene)
	return scene


func _check_pair(label: String, pair, surfaces: Array, rng: RandomNumberGenerator) -> void:
	if pair == null:
		return
	var bounds_list := [VisualConverter.DEFAULT_BOUNDS(), Rect2(560, 240, 800, 600)]
	for which in ["planned", "physical"]:
		var path = pair.get(which)
		var lbl := "%s %s" % [label, which]
		for b in bounds_list:
			var gd := GdVisualConverter.prepare_for_display(path, b, surfaces)
			var nt := VisualConverter.prepare_for_display(path, b, surfaces)
			_note(display_path_diff(gd, nt, "%s display %s" % [lbl, b]))
			for ds in nt.steps:
				var gd_dir: Vector2 = GdArrowAdvance._arc_interpolate(ds, 0.0).direction if GdArrowAdvance._is_arc(ds) else (ds.end - ds.start).normalized()
				_note("" if Diff.same_vec(gd_dir, ArrowAdvance.start_direction(ds)) else "%s start_direction" % lbl)
			# the arrow flight over the display path, in random increments
			var gr = null
			var nr = null
			var idx := 0
			var prog := 0.0
			var pos: Vector2 = nt.steps[0].start if nt.steps.size() > 0 else Vector2.ZERO
			for k in 12:
				var dist := rng.randf_range(0.0, 400.0)
				gr = GdArrowAdvance.advance(nt.steps, idx, prog, pos, dist, b)
				nr = ArrowAdvance.advance(nt.steps, idx, prog, pos, dist, b)
				var r := ""
				if gr.step_index != nr.step_index or gr.finished != nr.finished:
					r = "%s advance %d: step/finished (%d, %s) vs (%d, %s)" % [lbl, k, gr.step_index, gr.finished, nr.step_index, nr.finished]
				elif not Diff.same_float(gr.progress, nr.progress):
					r = "%s advance %d: progress %s vs %s" % [lbl, k, Diff.hex(gr.progress), Diff.hex(nr.progress)]
				elif not Diff.same_vec(gr.position, nr.position) or not Diff.same_vec(gr.direction, nr.direction):
					r = "%s advance %d: position/direction %s %s vs %s %s" % [lbl, k, gr.position, gr.direction, nr.position, nr.direction]
				_note(r)
				if r != "" or nr.finished:
					break
				idx = nr.step_index
				prog = nr.progress
				pos = nr.position
		for s in path.steps:
			_note("" if Diff.same_vec(GdVisualConverter.compute_display_end(s, surfaces), VisualConverter.compute_display_end(s, surfaces)) else "%s display_end" % lbl)
			if s.is_arc_step and not is_inf(s.end.x) and not is_inf(s.start.x):
				_note(arc_params_diff(GdVisualConverter.arc_params(s.start, s.via, s.end), VisualConverter.arc_params(s.start, s.via, s.end), lbl + " arc_params"))
		var gh: Dictionary = GdPhysicalHits.build_physical_hits(path)
		var nh: Dictionary = path.physical_hits()
		_note("" if gh == nh else "%s physical_hits: %s vs %s" % [lbl, gh, nh])
	# merged steps
	var ci: int = pair.planned.cursor_index
	_note(steps_diff(GdStepTreeMerge.merge(pair.planned.steps, pair.physical.steps, ci), StepTreeMerge.merge(pair.planned.steps, pair.physical.steps, ci), label + " merge"))
	_note(steps_diff(GdStepTreeMerge.classify_physical(pair.physical), StepTreeMerge.classify_physical(pair.physical), label + " classify"))


func test_sweep_scene_traces() -> void:
	var rng := _rng(100)
	var scenes := SweepPlans.discover_scenes(SweepPlans.TEST_LEVELS_DIR)
	var stride := 37
	var s := OS.get_environment("DIFF_STRIDE")
	if s.is_valid_int() and int(s) > 0:
		stride = int(s)
	for path in scenes:
		var scene := _instantiate(path)
		await get_tree().process_frame
		var runner := SweepRunner.new().configure(5, 10, 42)
		var positions := runner.build_positions(runner._extract_points_of_interest(scene))
		var plans: Array = [[]]
		if path in SweepPlans.plan_scenes():
			plans.append_array(SweepPlans.reflective_plans(scene).slice(0, 2))
		var idx := 0
		for plan in plans:
			for p in positions:
				for c in positions:
					if p == c:
						continue
					var i := idx
					idx += 1
					if i % stride != 0:
						continue
					var pair := TestTrace.trace_pair(scene.surfaces, p, c, plan)
					_check_pair("%s p=%s c=%s plan=%d" % [path.get_file().get_basename(), p, c, plan.size()], pair, scene.surfaces, rng)
	_report("sweep scene traces")
