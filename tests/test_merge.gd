extends GutTest

func _step(start: Vector2, end_v: Vector2, frame_id: int = 0) -> TraceStep:
	return TraceStep.new(start, end_v, frame_id)

func test_all_aligned() -> void:
	var steps: Array = [
		_step(Vector2(0, 0), Vector2(100, 0)),
		_step(Vector2(100, 0), Vector2(200, 0)),
	]
	var merged := StepTreeMerge.merge(steps, steps, 1)
	assert_eq(merged.size(), 2, "2 steps")
	var m0: TraceStep = merged[0]
	var m1: TraceStep = merged[1]
	assert_eq(m0.type, StepTypes.Type.ALIGNED, "Pre-cursor = ALIGNED")
	assert_eq(m1.type, StepTypes.Type.ALIGNED_POST_PLANNED, "Post-cursor = ALIGNED_POST_PLANNED")

func test_all_post_cursor() -> void:
	var steps: Array = [
		_step(Vector2(0, 0), Vector2(100, 0)),
		_step(Vector2(100, 0), Vector2(200, 0)),
	]
	var merged := StepTreeMerge.merge(steps, steps, 0)
	for i in merged.size():
		var ms: TraceStep = merged[i]
		assert_eq(ms.type, StepTypes.Type.ALIGNED_POST_PLANNED, "All post-cursor at step %d" % i)

func test_frame_divergence() -> void:
	var planned: Array = [
		_step(Vector2(0, 0), Vector2(100, 0), 0),
		_step(Vector2(100, 0), Vector2(200, 0), 5),
	]
	var physical: Array = [
		_step(Vector2(0, 0), Vector2(100, 0), 0),
		_step(Vector2(100, 0), Vector2(200, 0), 7),
	]
	var merged := StepTreeMerge.merge(planned, physical, 2)
	var m0: TraceStep = merged[0]
	assert_eq(m0.type, StepTypes.Type.ALIGNED, "Step 0 aligned")
	# Step 1: diverged — both planned and physical emitted
	var has_div_planned := false
	var has_div_physical := false
	for i in range(1, merged.size()):
		var ms: TraceStep = merged[i]
		if ms.type == StepTypes.Type.DIVERGED_PLANNED:
			has_div_planned = true
		if ms.type == StepTypes.Type.DIVERGED_PHYSICAL:
			has_div_physical = true
	assert_true(has_div_planned, "Should have DIVERGED_PLANNED")
	assert_true(has_div_physical, "Should have DIVERGED_PHYSICAL")

func test_cursor_boundary() -> void:
	var steps: Array = [
		_step(Vector2(0, 0), Vector2(100, 0)),
		_step(Vector2(100, 0), Vector2(200, 0)),
		_step(Vector2(200, 0), Vector2(300, 0)),
	]
	var merged := StepTreeMerge.merge(steps, steps, 2)
	var m0: TraceStep = merged[0]
	var m1: TraceStep = merged[1]
	var m2: TraceStep = merged[2]
	assert_eq(m0.type, StepTypes.Type.ALIGNED, "idx 0 < cursor_index=2")
	assert_eq(m1.type, StepTypes.Type.ALIGNED, "idx 1 < cursor_index=2")
	assert_eq(m2.type, StepTypes.Type.ALIGNED_POST_PLANNED, "idx 2 >= cursor_index=2")

func test_monotonic_divergence() -> void:
	var planned: Array = [
		_step(Vector2(0, 0), Vector2(100, 0), 0),
		_step(Vector2(100, 0), Vector2(200, 0), 5),
		_step(Vector2(200, 0), Vector2(300, 0), 5),
	]
	var physical: Array = [
		_step(Vector2(0, 0), Vector2(100, 0), 0),
		_step(Vector2(100, 0), Vector2(200, 0), 7),
		_step(Vector2(200, 0), Vector2(300, 0), 7),
	]
	var merged := StepTreeMerge.merge(planned, physical, 3)
	var diverged := false
	for i in merged.size():
		var ms: TraceStep = merged[i]
		if ms.type == StepTypes.Type.DIVERGED_PLANNED or ms.type == StepTypes.Type.DIVERGED_PHYSICAL:
			diverged = true
		elif diverged and ms.type == StepTypes.Type.ALIGNED:
			fail_test("ALIGNED after divergence at step %d — not monotonic" % i)
			return
	pass_test("Monotonic divergence")

func test_physical_shorter_than_planned() -> void:
	# Physical stops at terminal wall before cursor; planned continues
	var planned: Array = [
		_step(Vector2(0, 0), Vector2(100, 0), 0),
		_step(Vector2(100, 0), Vector2(200, 0), 0),
		_step(Vector2(200, 0), Vector2(300, 0), 0),
	]
	var physical: Array = [
		_step(Vector2(0, 0), Vector2(100, 0), 0),
	]
	var merged := StepTreeMerge.merge(planned, physical, 3)
	var m0: TraceStep = merged[0]
	assert_eq(m0.type, StepTypes.Type.ALIGNED, "Step 0 aligned")
	# Remaining planned steps should be DIVERGED_PLANNED (physical ran out)
	var div_planned_count := 0
	for i in merged.size():
		var ms: TraceStep = merged[i]
		if ms.type == StepTypes.Type.DIVERGED_PLANNED:
			div_planned_count += 1
	assert_eq(div_planned_count, 2, "2 remaining planned steps diverged")

func test_planned_shorter_than_physical() -> void:
	var planned: Array = [
		_step(Vector2(0, 0), Vector2(100, 0), 0),
	]
	var physical: Array = [
		_step(Vector2(0, 0), Vector2(100, 0), 0),
		_step(Vector2(100, 0), Vector2(200, 0), 0),
	]
	var merged := StepTreeMerge.merge(planned, physical, 1)
	var div_physical_count := 0
	for i in merged.size():
		var ms: TraceStep = merged[i]
		if ms.type == StepTypes.Type.DIVERGED_PHYSICAL:
			div_physical_count += 1
	assert_eq(div_physical_count, 1, "1 remaining physical step diverged")

func test_diverged_post_cursor() -> void:
	# Divergence after cursor → DIVERGED_POST_PLANNED
	var planned: Array = [
		_step(Vector2(0, 0), Vector2(100, 0), 0),
		_step(Vector2(100, 0), Vector2(200, 50), 5),
	]
	var physical: Array = [
		_step(Vector2(0, 0), Vector2(100, 0), 0),
		_step(Vector2(100, 0), Vector2(200, -50), 7),
	]
	var merged := StepTreeMerge.merge(planned, physical, 1)
	var m0: TraceStep = merged[0]
	assert_eq(m0.type, StepTypes.Type.ALIGNED, "Pre-cursor aligned")
	var has_post_planned := false
	for i in merged.size():
		var ms: TraceStep = merged[i]
		if ms.type == StepTypes.Type.DIVERGED_POST_PLANNED:
			has_post_planned = true
	assert_true(has_post_planned, "Should have DIVERGED_POST_PLANNED")
