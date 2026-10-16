class_name Surface
extends RefCounted

static var _next_id: int = 1

var id: int
var segment: Segment
var _left_config: SideConfig
var _right_config: SideConfig
# A target carries a TargetHit on a side (GAME_SPEC 10.6, 24); derived.
var is_target: bool:
	get:
		for cfg in all_side_configs():
			if cfg != null and cfg.first_hit(TargetHit) != null:
				return true
		return false
var player_solid: bool
# Stage 107: flag-gated existence, all required (GAME_SPEC 9.6); empty = always
var presence: Array[PresenceCond] = []
# Stage 101: part of the scene's pre-transform (a full inversion circle the
# shot starts "after"): never hit, never clickable, not drawn as a surface; it
# moves infinity before the shot
var pre_transform: bool = false
var _left_link: SideLink = null
var _right_link: SideLink = null
# Derived overlay links per side, reused while the side's config is the same
# object: {side: {"cfg": SideConfig, "link": SideLink, "pair": SideLink}}
# (Stage 103: built once instead of every hover / overlay update; the entry
# also keeps the pair's second link alive, which the first holds weakly)
var _derived_links := {}
var resolver: ConfigResolver

func _init(p_segment: Segment, p_left: SideConfig, p_right: SideConfig, p_is_target: bool = false, p_player_solid: bool = true) -> void:
	id = _next_id
	_next_id += 1
	segment = p_segment
	if p_is_target:
		p_left = _target_config(p_left)
		p_right = _target_config(p_right)
	_left_config = p_left
	_right_config = p_right
	resolver = FixedResolver.new(p_left, p_right)
	player_solid = p_player_solid
	_auto_link(Side.Value.LEFT, _left_config)
	_auto_link(Side.Value.RIGHT, _right_config)

func active_side_config(side: Side.Value, game_state: GameState) -> SideConfig:
	return resolver.resolve(side, game_state)

# Whether the surface exists under the flags (GAME_SPEC 9.6).
func present(flags: Dictionary) -> bool:
	return PresenceCond.all_hold(presence, flags)

# Every side config the resolver can return (both sides; every table entry).
func all_side_configs() -> Array[SideConfig]:
	var out: Array[SideConfig] = []
	if resolver is CategoricalResolver:
		for k in resolver.table:
			out.append(resolver.table[k][0])
			out.append(resolver.table[k][1])
	elif resolver is FixedResolver:
		out.append(resolver.left)
		out.append(resolver.right)
	return out

# A side config with a TargetHit (a pass side gets one too: a target
# registers whatever its effect, GAME_SPEC 24.2).
static func _target_config(cfg: SideConfig) -> SideConfig:
	return (cfg if cfg != null else SideConfig.new()).with_hits([TargetHit.new()])

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

# A pre-transform inversion circle (Stage 101, GAME_SPEC 9.5).
static func pre_transform_circle(center: Vector2, radius: float) -> Surface:
	var carrier := GeneralizedCircle.from_circle(center, radius)
	var inv := CircleInversionEffect.new(carrier)
	var cfg := SideConfig.new(inv, false)
	var s := Surface.new(Segment.full_from_carrier(carrier), cfg, cfg, false, false)
	s.pre_transform = true
	return s

# A surface whose side configs depend on the game state (a categorical
# resolver). Self-links are for fixed sides only.
static func with_resolver(p_segment: Segment, p_resolver: ConfigResolver, p_is_target: bool = false, p_player_solid: bool = true) -> Surface:
	var s := Surface.new(p_segment, null, null, false, p_player_solid)
	if p_is_target and p_resolver is CategoricalResolver:
		for k in p_resolver.table:
			p_resolver.table[k] = [_target_config(p_resolver.table[k][0]), _target_config(p_resolver.table[k][1])]
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
	var cached: Dictionary = _derived_links.get(side, {})
	if not cached.is_empty() and is_same(cached["cfg"], cfg):
		return cached["link"]
	link = SideLink.new()
	link.surface = self
	link.side = side
	var e := cfg.effect
	var pair: SideLink = null   # held here: `link` refers to it weakly
	if e == null:
		pair = _opposite_link(side, link)
		link.outgoing = pair
	elif e.is_terminal():
		link.outgoing = null
	elif e.is_self_inverse():
		link.outgoing = link
	elif e.kind() == Effect.Kind.PROJECTIVE and e.get("_project_back"):
		link.outgoing = link
	else:
		pair = _opposite_link(side, link)
		link.outgoing = pair
	_derived_links[side] = {"cfg": cfg, "link": link, "pair": pair}
	return link

func _opposite_link(side: Side.Value, back: SideLink) -> SideLink:
	var o := SideLink.new()
	o.surface = self
	o.side = Side.Value.RIGHT if side == Side.Value.LEFT else Side.Value.LEFT
	o.outgoing = back
	return o

static func reset_id_counter() -> void:
	_next_id = 1
