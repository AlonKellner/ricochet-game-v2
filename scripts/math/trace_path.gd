class_name TracePath
extends RefCounted

# A traced path: steps plus trace-level results. Formerly Tracer.TracedPath
# (still available under that name as an alias).

var steps: Array = []
var targets_hit: Dictionary = {}
var cursor_index: int = -1
var hit_count: int = 0
