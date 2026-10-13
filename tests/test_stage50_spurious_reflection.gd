extends GutTest

const H := preload("res://tests/test_helpers.gd")

func before_each() -> void:
	H.reset_counters()

func _build_three_mirrors_surfaces() -> Array:
	var surfaces: Array = []
	# 3 block walls (no right wall, matching three_mirrors.tscn)
	# Top: (560,240) -> (1360,240)
	surfaces.append(RoomBuilder.create_block_surface(
		Vector2(560, 240), Vector2(1360, 240), Vector2(960, 240)))
	# Bottom: (1360,840) -> (560,840)
	surfaces.append(RoomBuilder.create_block_surface(
		Vector2(1360, 840), Vector2(560, 840), Vector2(960, 840)))
	# Left: (560,840) -> (560,240)
	surfaces.append(RoomBuilder.create_block_surface(
		Vector2(560, 840), Vector2(560, 240), Vector2(560, 540)))
	# Mirror A: x=800, y=[300,780], L=reflect
	var seg_a := Segment.from_coords(Vector2(800, 300), Vector2(800, 780), Vector2(800, 540))
	var refl_a := ReflectionEffect.new(seg_a.get_carrier())
	surfaces.append(Surface.new(seg_a, SideConfig.new(refl_a, true), SideConfig.new(null, false), false, false))
	# Mirror C: x=1200, y=[300,780], L=reflect
	var seg_c := Segment.from_coords(Vector2(1200, 300), Vector2(1200, 780), Vector2(1200, 540))
	var refl_c := ReflectionEffect.new(seg_c.get_carrier())
	surfaces.append(Surface.new(seg_c, SideConfig.new(refl_c, true), SideConfig.new(null, false), false, false))
	# Mirror B: x=1000, y=[400,700], R=reflect
	var seg_b := Segment.from_coords(Vector2(1000, 400), Vector2(1000, 700), Vector2(1000, 550))
	var refl_b := ReflectionEffect.new(seg_b.get_carrier())
	surfaces.append(Surface.new(seg_b, SideConfig.new(null, false), SideConfig.new(refl_b, true), false, false))
	# Screen bounds (matching level_settings.gd)
	var screen_bounds := [
		Vector4(0, 0, 1920, 0),
		Vector4(1920, 0, 1920, 1080),
		Vector4(1920, 1080, 0, 1080),
		Vector4(0, 1080, 0, 0),
	]
	for line_def in screen_bounds:
		var seg := Segment.from_coords(
			Vector2(line_def.x, line_def.y), Vector2(line_def.z, line_def.w),
			Vector2((line_def.x + line_def.z) / 2.0, (line_def.y + line_def.w) / 2.0))
		var config := SideConfig.new(null, false)
		surfaces.append(Surface.new(seg, config, config, false, false))
	return surfaces

func test_multi_bounce_valid_hits() -> void:
	var surfaces := _build_three_mirrors_surfaces()
	var player := Vector2(1108.732, 827.924)
	var aim := Direction.from_coords(player, Vector2(853.8895, 710.3155))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	var bad_hits := 0
	for i in range(1, path.steps.size()):
		var step: TraceStep = path.steps[i]
		var prev: TraceStep = path.steps[i - 1]
		if step.frame_id == prev.frame_id:
			continue
		if prev.hit == null or not prev.hit.on_segment:
			bad_hits += 1

	assert_eq(bad_hits, 0,
		"Every reflection-triggering hit should be on a valid mirror segment (on_seg=true)")

func test_repro_numerical_stability() -> void:
	var surfaces := _build_three_mirrors_surfaces()
	var player := Vector2(1108.732, 827.924)
	var aim := Direction.from_coords(player, Vector2(853.8895, 710.3155))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	for i in path.steps.size():
		var step: TraceStep = path.steps[i]
		if is_inf(step.start.x) or is_inf(step.start.y):
			continue
		if is_inf(step.end.x) or is_inf(step.end.y):
			continue
		assert_lt(absf(step.start.x), 1e6,
			"Step %d start.x = %.2f exceeds 1e6 (numerical instability)" % [i, step.start.x])
		assert_lt(absf(step.start.y), 1e6,
			"Step %d start.y = %.2f exceeds 1e6 (numerical instability)" % [i, step.start.y])
		assert_lt(absf(step.end.x), 1e6,
			"Step %d end.x = %.2f exceeds 1e6 (numerical instability)" % [i, step.end.x])
		assert_lt(absf(step.end.y), 1e6,
			"Step %d end.y = %.2f exceeds 1e6 (numerical instability)" % [i, step.end.y])

func test_multi_bounce_all_on_segment() -> void:
	var surfaces := _build_three_mirrors_surfaces()
	var player := Vector2(1108.732, 827.924)
	var aim := Direction.from_coords(player, Vector2(853.8895, 710.3155))
	var path := TestTrace.trace(player, aim, surfaces, GameState.new())

	var bad_hits := 0
	for i in range(1, path.steps.size()):
		var step: TraceStep = path.steps[i]
		var prev: TraceStep = path.steps[i - 1]
		if step.frame_id == prev.frame_id:
			continue
		if prev.hit == null or not prev.hit.on_segment:
			bad_hits += 1
			print("BAD step %d: prev frame_id=%d on_seg=%s" % [
				i - 1, prev.frame_id,
				str(prev.hit.on_segment) if prev.hit else "null"])

	assert_eq(bad_hits, 0,
		"Every reflection-triggering hit should have on_seg=true")

# --- Sweep ---

func test_sweep_multi_bounce_validity() -> void:
	var surfaces := _build_three_mirrors_surfaces()
	var unstable_count := 0
	var total := 0

	for px in range(600, 1350, 50):
		for py in range(250, 840, 50):
			var player := Vector2(px, py)
			for angle_deg in range(0, 360, 10):
				var angle := deg_to_rad(angle_deg)
				var target := player + Vector2(cos(angle), sin(angle)) * 100.0
				var aim := Direction.from_coords(player, target)
				H.reset_counters()
				var path := TestTrace.trace(player, aim, surfaces, GameState.new())
				total += 1

				for step in path.steps:
					var s: TraceStep = step
					if is_inf(s.end.x) or is_inf(s.end.y):
						continue
					if absf(s.end.x) > 1e6 or absf(s.end.y) > 1e6:
						unstable_count += 1
						break

	assert_eq(unstable_count, 0,
		"No traces should have numerical instability. Found %d/%d (%.1f%%)" % [
			unstable_count, total, 100.0 * unstable_count / total if total > 0 else 0.0])
