class_name SideConfig
extends RefCounted

# One side's behaviour on two orthogonal axes (GAME_SPEC 9.1): exactly one
# path effect (null = pass), and an ordered list of hit effects (10.6).

var effect: Effect
var hit_effects: Array[HitEffect] = []
var interactive: bool

func _init(p_effect: Effect = null, p_interactive: bool = false, p_hit_effects: Array = []) -> void:
	effect = p_effect
	interactive = p_interactive
	hit_effects.assign(p_hit_effects)

# The flag writes the solver applies when this side fires, in order.
func in_shot_writes(host_id: int) -> Array[StateChange]:
	var out: Array[StateChange] = []
	for h in hit_effects:
		out.append_array(h.in_shot_writes(host_id))
	return out

# Whether any hit effect writes `key` (in shot or at landing).
func writes_key(key: String, host_id: int) -> bool:
	for h in hit_effects:
		if h.writes_key(key, host_id):
			return true
	return false

# The first hit effect of a class (null if none).
func first_hit(script: Script) -> HitEffect:
	for h in hit_effects:
		if is_instance_of(h, script):
			return h
	return null

# A copy with more hit effects appended (configs are shared between sides).
func with_hits(more: Array) -> SideConfig:
	return SideConfig.new(effect, interactive, hit_effects + more)
