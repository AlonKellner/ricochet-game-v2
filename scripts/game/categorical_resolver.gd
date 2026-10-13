class_name CategoricalResolver
extends ConfigResolver

# The configs of a game-state flag's values (GAME_SPEC 9.4): table maps a
# value's StateChange.flag_key to [left, right]. A missing key, or a value
# without an entry, falls back to the "DEFAULT" entry, then to the first entry
# (with a warning). Portals and projectives are not categorical.

var state_key: String
var table: Dictionary

func _init(p_key: String = "", p_table: Dictionary = {}) -> void:
	state_key = p_key
	table = p_table
	assert(not table.is_empty(), "CategoricalResolver needs a table")

func entry(flags: Dictionary) -> Array:
	if flags.has(state_key):
		var k := StateChange.flag_key(flags[state_key])
		if table.has(k):
			return table[k]
	if table.has("DEFAULT"):
		return table["DEFAULT"]
	if flags.has(state_key):
		push_warning("CategoricalResolver '%s': no entry for %s and no DEFAULT" % [state_key, str(flags[state_key])])
	return table.values()[0]

func resolve(side: Side.Value, game_state: GameState) -> SideConfig:
	var e := entry(game_state.flags if game_state != null else {})
	return e[0] if side == Side.Value.LEFT else e[1]
