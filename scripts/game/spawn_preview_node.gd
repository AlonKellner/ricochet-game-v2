class_name SpawnPreviewNode
extends Node2D

# The spawn preview (Stage 107, GAME_SPEC 10.6): what the aimed shot would do
# to the world when it lands -- dashed ghost outlines of the surfaces it would
# spawn, a red dashed mark on the surfaces it would take away. It folds the
# path renderer's own trace with the landing's own fold (LandingFold) against
# the current world, so the preview is the landing (PREVIEW-EQUALS-LANDING);
# it builds no Surface (that would consume ids) and writes no state.

const GHOST_COLOR := Color(0.6, 0.95, 1.0, 0.8)
const VANISH_COLOR := Color(1.0, 0.35, 0.3, 0.7)
const DASH := 8.0
const WIDTH := 2.0

var _renderer: Node2D
var _level: Node2D
var _inputs = null
# the last preview: [{"channel", "generation", "spec"}] and the surfaces that would leave
var placed: Array = []
var vanishing: Array[Surface] = []

func _ready() -> void:
	process_mode = Node.PROCESS_MODE_ALWAYS
	_level = get_parent()
	_renderer = _level.get_node_or_null("PathRenderer")
	z_index = 19

func _process(_delta: float) -> void:
	if _renderer == null:
		return
	modulate = _renderer.modulate   # dimmed with the path during a flight
	var world: World = _level.world if "world" in _level else null
	var inputs := [_renderer.trace_version, world.surfaces if world != null else null]
	if _inputs != null and inputs[0] == _inputs[0] and is_same(inputs[1], _inputs[1]):
		return
	_inputs = inputs
	recompute()
	queue_redraw()

# The preview of the renderer's last trace (public: the tests call it).
func recompute() -> void:
	placed = []
	vanishing = []
	var world: World = _level.world if "world" in _level else null
	var pair: BeamTracerPair = _renderer.last_pair if _renderer != null else null
	if world == null or pair == null:
		return
	var landing := LandingFold.fold(pair.physical.hits, world.by_id(), pair.final_flags, world.generations)
	placed = landing["placed"]
	var gm := _level.get_node_or_null("GameManager")
	var now: Dictionary = gm.game_state.flags if gm != null else {}
	var after: Dictionary = landing["flags"]
	for s in world.surfaces:
		if not s.presence.is_empty() and s.present(now) and not s.present(after):
			vanishing.append(s)

func _draw() -> void:
	for p in placed:
		var spec: SurfaceSpec = p["spec"]
		dashed(self, spec.segment(), GHOST_COLOR, WIDTH)
		if spec.is_portal():
			var e: Dictionary = spec.left.effect
			var res := RigidMotionEffect.create_portal_pair(spec.segment(), float(e["theta"]), EffectSpec.vec(e["d"]))
			dashed(self, res.target_segment, GHOST_COLOR, WIDTH)
	for s in vanishing:
		dashed(self, s.segment, VANISH_COLOR, WIDTH)

# A segment drawn dashed on a canvas item (also SurfaceNode's absent outline).
static func dashed(ci: CanvasItem, seg: Segment, color: Color, width: float) -> void:
	if seg.is_line():
		ci.draw_dashed_line(seg.start.coords, seg.end.coords, color, width, DASH)
		return
	var carrier := seg.get_carrier()
	var c := carrier.center()
	var r := carrier.radius()
	var a0 := 0.0
	var span := TAU
	if not seg.full:
		a0 = (seg.start.coords - c).angle()
		var am := wrapf((seg.via.coords - c).angle() - a0, 0.0, TAU)
		var a1 := wrapf((seg.end.coords - c).angle() - a0, 0.0, TAU)
		span = a1 if am < a1 else a1 - TAU
	var n := maxi(8, int(absf(span) * r / DASH))
	for i in range(0, n, 2):
		var t0 := a0 + span * i / n
		var t1 := a0 + span * (i + 1) / n
		ci.draw_arc(c, r, t0, t1, 4, color, width)
