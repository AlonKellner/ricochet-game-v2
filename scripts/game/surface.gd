class_name Surface
extends RefCounted

static var _next_id: int = 1

var id: int
var segment: Segment
var _left_config: SideConfig
var _right_config: SideConfig
var is_target: bool
var player_solid: bool
var _left_link: SideLink = null
var _right_link: SideLink = null
var resolver: ConfigResolver

func _init(p_segment: Segment, p_left: SideConfig, p_right: SideConfig, p_is_target: bool = false, p_player_solid: bool = true) -> void:
	id = _next_id
	_next_id += 1
	segment = p_segment
	_left_config = p_left
	_right_config = p_right
	resolver = FixedResolver.new(p_left, p_right)
	is_target = p_is_target
	player_solid = p_player_solid
	_auto_link(Side.Value.LEFT, _left_config)
	_auto_link(Side.Value.RIGHT, _right_config)

func active_side_config(side: Side.Value, game_state: GameState) -> SideConfig:
	return resolver.resolve(side, game_state)

func get_side_link(side: Side.Value) -> SideLink:
	return _left_link if side == Side.Value.LEFT else _right_link

func set_side_link(side: Side.Value, link: SideLink) -> void:
	if side == Side.Value.LEFT:
		_left_link = link
	else:
		_right_link = link

func _auto_link(side: Side.Value, config: SideConfig) -> void:
	if config == null or not config.interactive:
		return
	if config.effect != null and config.effect.is_self_inverse():
		set_side_link(side, SideLink.from_self(self, side))

# A surface whose side configs depend on the game state (a categorical
# resolver). Self-links are for fixed sides only.
static func with_resolver(p_segment: Segment, p_resolver: ConfigResolver, p_is_target: bool = false, p_player_solid: bool = true) -> Surface:
	var s := Surface.new(p_segment, null, null, p_is_target, p_player_solid)
	s.resolver = p_resolver
	return s

# The link the plan / hover chevrons draw for a side: an explicit link
# (portal pairs, self-inverse self-links), else one derived from the side's
# config under the game state -- every interactive side has one. Where the
# beam leaves: the same side (reflection, inversion, a projective "back"),
# the opposite side (a pass, a toggler, a projective "through"), or nowhere
# (a block). null for a non-interactive side.
func overlay_link(side: Side.Value, game_state: GameState = null) -> SideLink:
	var cfg := active_side_config(side, game_state if game_state != null else GameState.new())
	if cfg == null or not cfg.interactive:
		return null
	var link := get_side_link(side)
	if link != null:
		return link
	link = SideLink.new()
	link.surface = self
	link.side = side
	var e := cfg.effect
	if e == null:
		link.outgoing = _opposite_link(side, link)
	elif e.is_terminal():
		link.outgoing = null
	elif e.is_self_inverse():
		link.outgoing = link
	elif e.kind() == Effect.Kind.PROJECTIVE and e.get("_project_back"):
		link.outgoing = link
	else:
		link.outgoing = _opposite_link(side, link)
	return link

func _opposite_link(side: Side.Value, back: SideLink) -> SideLink:
	var o := SideLink.new()
	o.surface = self
	o.side = Side.Value.RIGHT if side == Side.Value.LEFT else Side.Value.LEFT
	o.outgoing = back
	return o

static func reset_id_counter() -> void:
	_next_id = 1
