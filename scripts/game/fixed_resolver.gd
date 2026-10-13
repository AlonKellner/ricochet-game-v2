class_name FixedResolver
extends ConfigResolver

# One fixed config per side, independent of the game state.

var left: SideConfig
var right: SideConfig

func _init(p_left: SideConfig = null, p_right: SideConfig = null) -> void:
	left = p_left
	right = p_right

func resolve(side: Side.Value, _game_state: GameState) -> SideConfig:
	return left if side == Side.Value.LEFT else right
