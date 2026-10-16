class_name SpawnHit
extends HitEffect

# A spawn (GAME_SPEC 10.6): in the shot it consumes its hosts (the channel
# becomes "pending": every surface spawning into it is absent by presence);
# when the arrow lands -- always, it has no in-shot form -- the template is
# placed at the hit that last fired it and the channel holds the new group.

var channel: String
var template: SpawnTemplate

func _init(p_channel: String = "", p_template: SpawnTemplate = null, p_deferred := false) -> void:
	channel = p_channel
	template = p_template
	deferred = p_deferred

func writes(_host_id: int) -> Array[StateChange]:
	return [StateChange.new(Channel.key(channel), StateChange.Op.SET, Channel.PENDING)]

func has_landing_part() -> bool:
	return true

func to_spec() -> Dictionary:
	return {"kind": "spawn", "deferred": deferred, "channel": channel, "template": template}

func get_display_name() -> String:
	return "spawn into %s" % channel
