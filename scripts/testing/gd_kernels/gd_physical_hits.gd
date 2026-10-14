class_name GdPhysicalHits
extends RefCounted

# Test-support copy of PathRenderer.build_physical_hits for
# tests/native/test_diff_visual.gd; the game uses TracePath.physical_hits()
# (native). Deleted in Stage 99.

static func build_physical_hits(path: TracePath) -> Dictionary:
	var result := {}
	for i in path.steps.size():
		var s: TraceStep = path.steps[i]
		if s.surface_id < 0:
			continue
		if not result.has(s.surface_id):
			result[s.surface_id] = []
		var has_continuation := i < path.steps.size() - 1
		result[s.surface_id].append({"side": s.hit_side, "on_segment": s.hit_on_segment, "has_continuation": has_continuation})
	return result

