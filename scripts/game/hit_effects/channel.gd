class_name Channel
extends RefCounted

# A spawn channel's flag (GAME_SPEC 10.6): "chan:<name>" is missing or
# "armed", "pending" while a spawn awaits landing, or "g<N>" while the N-th
# spawned group exists. Presence (9.6) reads it: hosts exist while it is
# armed, generation N's group while it is "g<N>".

const PREFIX := "chan:"
const ARMED := "armed"
const PENDING := "pending"

static func key(name: String) -> String:
	return PREFIX + name

static func generation_value(n: int) -> String:
	return "g%d" % n

# The generation of a channel value ("g<N>" -> N), 0 for any other value.
static func generation_of(value) -> int:
	if value is String and value.begins_with("g") and value.substr(1).is_valid_int():
		return int(value.substr(1))
	return 0

# The presence of a channel's hosts: armed (a missing key is armed).
static func host_presence(name: String) -> PresenceCond:
	return PresenceCond.new(key(name), PackedStringArray([PresenceCond.DEFAULT, ARMED]))

# The presence of generation n's group.
static func group_presence(name: String, n: int) -> PresenceCond:
	return PresenceCond.new(key(name), PackedStringArray([generation_value(n)]))
