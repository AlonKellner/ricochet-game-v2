class_name TracePath
extends RefCounted

# A traced path: steps plus trace-level results (BeamTracer.to_path).

var steps: Array = []
var targets_hit: Dictionary = {}
var cursor_index: int = -1
var hit_count: int = 0
