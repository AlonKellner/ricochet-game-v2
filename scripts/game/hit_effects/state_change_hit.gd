class_name StateChangeHit
extends HitEffect

# A flag write (GAME_SPEC 10.6.2): SET a value, or TOGGLE a truth value. The
# ghost toggler is StateChangeHit("ghost", TOGGLE).

var change: StateChange

func _init(p_key: String = "", p_op: int = StateChange.Op.SET, p_value = true, p_deferred := false) -> void:
	change = StateChange.new(p_key, p_op, p_value)
	deferred = p_deferred

func writes(_host_id: int) -> Array[StateChange]:
	return [change]

func to_spec() -> Dictionary:
	var d := change.to_def()
	d["kind"] = "state"
	d["deferred"] = deferred
	return d

func get_display_name() -> String:
	return "toggle %s" % change.key if change.op == StateChange.Op.TOGGLE else "set %s" % change.key
