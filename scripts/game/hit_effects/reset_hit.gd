class_name ResetHit
extends HitEffect

# A reset (GAME_SPEC 10.6): arms its channels. Their spawned groups vanish and
# their hosts reappear -- in the same shot, by presence (9.6) -- unless
# deferred to landing.

var channels: PackedStringArray

func _init(p_channels: PackedStringArray = PackedStringArray(), p_deferred := false) -> void:
	channels = p_channels
	deferred = p_deferred

func writes(_host_id: int) -> Array[StateChange]:
	var out: Array[StateChange] = []
	for c in channels:
		out.append(StateChange.new(Channel.key(c), StateChange.Op.SET, Channel.ARMED))
	return out

func to_spec() -> Dictionary:
	return {"kind": "reset", "deferred": deferred, "channels": Array(channels)}

func get_display_name() -> String:
	return "reset %s" % ", ".join(channels)
