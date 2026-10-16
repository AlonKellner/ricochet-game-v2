class_name SpawnTemplate
extends Resource

# What a spawn places (GAME_SPEC 10.6.1): a surface in a local frame -- x along
# the mode's axis u, y along v -- mapped to the hit by SpawnFrame. Any
# carrier, path effect, hit effects and presence; not another spawn. The
# surface is authored centred on the local origin, which lands on the hit
# point; `offset` (local, default none) moves it away deliberately.

@export var surface: SurfaceSpec
@export var orientation: SpawnFrame.Mode = SpawnFrame.Mode.CARRIER_TANGENT
@export var mirror_with_side := false
@export var offset := Vector2.ZERO

static func make(p_surface: SurfaceSpec, p_orientation: SpawnFrame.Mode, p_mirror := false, p_offset := Vector2.ZERO) -> SpawnTemplate:
	var t := SpawnTemplate.new()
	t.surface = p_surface
	t.orientation = p_orientation
	t.mirror_with_side = p_mirror
	t.offset = p_offset
	return t

# The surface spec placed by a frame (the offset is in the frame's axes).
func place(frame: Transform2D) -> SurfaceSpec:
	return surface.mapped(frame * Transform2D(0.0, offset))

func validate() -> Array[String]:
	var out: Array[String] = []
	if surface == null:
		out.append("a spawn template needs a surface")
		return out
	out.append_array(surface.validate())
	var sides: Array = [surface.left, surface.right]
	for k in surface.resolver_table:
		sides.append_array(surface.resolver_table[k])
	for s: SideSpec in sides:
		if s == null:
			continue
		for h in s.hits:
			if h.get("kind", "") == "spawn":
				out.append("a spawned surface cannot spawn (no nesting, GAME_SPEC 10.6)")
	return out

func to_spec() -> Dictionary:
	return {"surface": surface, "orientation": orientation, "mirror_with_side": mirror_with_side}
