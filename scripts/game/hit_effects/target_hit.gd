class_name TargetHit
extends HitEffect

# A target (GAME_SPEC 10.6, 24): registers by writing "target:<surface id>".
# targets_hit and the win check read these flags.

const PREFIX := "target:"

func _init(p_deferred := false) -> void:
	deferred = p_deferred

static func key_of(host_id: int) -> String:
	return PREFIX + str(host_id)

func writes(host_id: int) -> Array[StateChange]:
	return [StateChange.new(key_of(host_id), StateChange.Op.SET, true)]

func to_spec() -> Dictionary:
	return {"kind": "target", "deferred": deferred}

func get_display_name() -> String:
	return "target"
