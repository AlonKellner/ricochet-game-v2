class_name StepTreeMerge
extends RefCounted

static func classify_physical(path: TracePath) -> Array:
	var result: Array = []
	var ci: int = path.cursor_index
	if ci < 0:
		ci = path.steps.size()
	for idx in path.steps.size():
		var s: TraceStep = path.steps[idx]
		var step_type: int
		if idx < ci:
			step_type = StepTypes.Type.ALIGNED
		else:
			step_type = StepTypes.Type.ALIGNED_POST_PLANNED
		result.append(s.with_type(step_type))
	return result

static func merge(planned_steps: Array, physical_steps: Array, cursor_index: int) -> Array:
	var merged: Array = []
	var diverged := false
	var max_len: int = maxi(planned_steps.size(), physical_steps.size())

	for idx in max_len:
		var p: TraceStep = planned_steps[idx] if idx < planned_steps.size() else null
		var r: TraceStep = physical_steps[idx] if idx < physical_steps.size() else null
		var past_cursor: bool = idx >= cursor_index

		if not diverged and p != null and r != null and p.frame_id == r.frame_id:
			var step_type: int
			if past_cursor:
				step_type = StepTypes.Type.ALIGNED_POST_PLANNED
			else:
				step_type = StepTypes.Type.ALIGNED
			merged.append(p.with_type(step_type))
		else:
			diverged = true
			if p != null:
				var div_type: int
				if past_cursor:
					div_type = StepTypes.Type.DIVERGED_POST_PLANNED
				else:
					div_type = StepTypes.Type.DIVERGED_PLANNED
				merged.append(p.with_type(div_type))
			if r != null:
				merged.append(r.with_type(StepTypes.Type.DIVERGED_PHYSICAL))

	return merged
