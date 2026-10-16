class_name HitEffect
extends RefCounted

# A side's hit effect (GAME_SPEC 10.6): the second axis of a side, orthogonal
# to its path effect. A side carries an ordered list of them. Each fires when
# its side fires; its IN_SHOT part is a list of flag writes the solver applies
# during the walk, its ON_LANDING part runs when the arrow lands
# (LandingFold). `deferred` moves the writes to landing (only for an effect
# whose writes are its whole in-shot form).

var deferred := false

# The flag writes of this effect for the surface carrying it.
func writes(_host_id: int) -> Array[StateChange]:
	return []

# The writes the solver applies in the shot ([] when deferred).
func in_shot_writes(host_id: int) -> Array[StateChange]:
	return [] if deferred else writes(host_id)

# The writes applied when the arrow lands ([] unless deferred).
func landing_writes(host_id: int) -> Array[StateChange]:
	return writes(host_id) if deferred else []

# Whether a landing part beyond deferred writes exists (a spawn's placement).
func has_landing_part() -> bool:
	return false

# The flags this effect writes, as keys (for the HUD and the toggler colours).
func writes_key(key: String, host_id: int) -> bool:
	for w in writes(host_id):
		if w.key == key:
			return true
	return false

# Data form: {"kind": ..., "deferred": bool, ...}; HitEffectSpec.build reads it.
func to_spec() -> Dictionary:
	return {"kind": "none", "deferred": deferred}

func get_display_name() -> String:
	return "hit effect"
