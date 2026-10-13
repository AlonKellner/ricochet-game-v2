extends GutTest

# Stage 80: neutral trace types (independent of the legacy Tracer) and
# step-metadata preservation through VisualConverter.

# "frame" (the legacy Mobius frame, always null on the beam) is deliberately
# not listed: TraceStep.frame is being removed in Stage 84.
const STEP_FIELDS := ["start", "end", "via", "frame_id", "hit", "ray", "is_arc_step", "type",
	"surface_id", "hit_side", "hit_on_segment", "after_portal", "est_error", "precision_bits"]
const PATH_FIELDS := ["steps", "targets_hit", "cursor_index", "hit_count"]
const HIT_FIELDS := ["t", "point", "segment", "side", "on_segment", "at_endpoint", "blocked_left", "blocked_right"]

func test_trace_step_fields_and_aliases():
	var s = TraceStep.new(Vector2(0, 0), Vector2(1, 0), 0, null)
	assert_true(s is TraceStep, "TraceStep is an alias of TraceStep")
	for f in STEP_FIELDS:
		assert_true(f in s, "TraceStep has field %s" % f)
	var p = TracePath.new()
	assert_true(p is TracePath, "TracePath is an alias of TracePath")
	for f in PATH_FIELDS:
		assert_true(f in TracePath.new(), "TracePath has field %s" % f)
	var seg := Segment.from_coords(Vector2(0, 0), Vector2(10, 0), Vector2(5, 0))
	var h = TraceHit.new(0.5, Vector2(5, 0), seg, Side.Value.LEFT, true)
	assert_true(h is TraceHit, "TraceHit is an alias of TraceHit")
	for f in HIT_FIELDS:
		assert_true(f in h, "TraceHit has field %s" % f)

# A hit step that goes the long way round through infinity (via = INF, both
# ends on screen): prepare_for_display splits it into start -> bounds and
# bounds -> end, and the second piece carries the hit.
func _meta_step() -> TraceStep:
	var seg := Segment.from_coords(Vector2(1500, 400), Vector2(1500, 600), Vector2(1500, 500))
	var hit := TraceHit.new(-1.0, Vector2(1500, 500), seg, Side.Value.LEFT, true)
	var step := TraceStep.new(Vector2(100, 500), Vector2(1500, 500), 0, hit, null, null, Vector2(INF, INF), false)
	step.surface_id = 5
	step.hit_side = Side.Value.LEFT
	step.hit_on_segment = true
	step.after_portal = true
	step.est_error = 0.25
	step.precision_bits = 112
	return step

func test_converter_keeps_meta_on_split():
	var path := TracePath.new()
	path.steps = [_meta_step()]
	var out := VisualConverter.prepare_for_display(path, VisualConverter.DEFAULT_BOUNDS, [])
	var with_hit: Array = out.steps.filter(func(s): return s.hit != null)
	assert_eq(with_hit.size(), 1, "exactly one display piece carries the hit")
	if with_hit.is_empty():
		return
	var s = with_hit[0]
	assert_eq(s.surface_id, 5, "surface_id kept")
	assert_eq(s.hit_side, Side.Value.LEFT, "hit_side kept")
	assert_true(s.hit_on_segment, "hit_on_segment kept")
	assert_eq(s.est_error, 0.25, "est_error kept")
	assert_eq(s.precision_bits, 112, "precision_bits kept")
	# after_portal belongs to the piece that starts the step
	assert_true(out.steps[0].after_portal, "after_portal kept on the first piece")

func test_physical_hits_survive_clipping():
	var path := TracePath.new()
	path.steps = [_meta_step()]
	var out := VisualConverter.prepare_for_display(path, VisualConverter.DEFAULT_BOUNDS, [])
	var hits := PathRenderer.build_physical_hits(out)
	assert_true(hits.has(5), "a surface hit on a clipped step is still listed")
