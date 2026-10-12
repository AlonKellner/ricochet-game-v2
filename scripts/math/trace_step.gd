class_name TraceStep
extends RefCounted

# One displayed step of a trace (visual space). Formerly Tracer.Step (still
# available under that name as an alias). Produced by both solvers.

var start: Vector2
var end: Vector2
var via: Vector2
var frame_id: int
var hit: TraceHit
var ray: Ray
var frame: MobiusTransform
var is_arc_step: bool
var type: int = StepTypes.Type.ALIGNED
var surface_id: int = -1
var hit_side: int = -1
var hit_on_segment: bool = false
var after_portal: bool = false
var est_error: float = -1.0     # estimated px error of this step's end (BeamTracer), -1 = unknown
var precision_bits: int = 0     # arithmetic precision the step was computed at (0 = float32 Tracer)

func _init(p_start: Vector2 = Vector2.ZERO, p_end: Vector2 = Vector2.ZERO, p_frame_id: int = 0, p_hit: TraceHit = null, p_ray: Ray = null, p_frame: MobiusTransform = null, p_via: Vector2 = Vector2.ZERO, p_is_arc: bool = false) -> void:
	start = p_start
	end = p_end
	frame_id = p_frame_id
	hit = p_hit
	ray = p_ray
	frame = p_frame
	via = p_via if p_via != Vector2.ZERO else (p_start + p_end) / 2.0
	is_arc_step = p_is_arc

func with_type(new_type: int) -> TraceStep:
	var copy := TraceStep.new(start, end, frame_id, hit, ray, frame, via, is_arc_step)
	copy.type = new_type
	copy.copy_meta_from(self)
	return copy

# Copy the per-step metadata (not geometry) from another step.
func copy_meta_from(o: TraceStep) -> void:
	surface_id = o.surface_id
	hit_side = o.hit_side
	hit_on_segment = o.hit_on_segment
	after_portal = o.after_portal
	est_error = o.est_error
	precision_bits = o.precision_bits
