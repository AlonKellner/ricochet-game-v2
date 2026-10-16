class_name SurfaceSpec
extends Resource

# A surface as data (Stage 107): geometry (three points, or a full circle),
# both sides on both axes (SideSpec), an optional categorical table, presence
# and player collision. build() makes the Surface (two for a portal: the
# pair); of() is its inverse; mapped() maps it through a rigid frame
# (spawn templates). Any combination of carrier, path effect, hit effects and
# presence is data -- no code per combination.

@export var start := Vector2.ZERO
@export var end := Vector2.ZERO
@export var via := Vector2.ZERO
@export var full_circle := false
@export var center := Vector2.ZERO
@export var radius := 0.0
@export var left: SideSpec
@export var right: SideSpec
# A categorical resolver (GAME_SPEC 9.4): flag_key -> [SideSpec, SideSpec]
# (left / right are then unused); empty = fixed sides.
@export var resolver_key := ""
@export var resolver_table: Dictionary = {}
# Presence (9.6): [{"key", "values"}], all required; empty = always.
@export var presence: Array[Dictionary] = []
@export var player_solid := false

# A line from a to b (via at the midpoint, as the level arrays build it).
static func line(a: Vector2, b: Vector2, p_left: SideSpec, p_right: SideSpec, p_player_solid := false) -> SurfaceSpec:
	return arc(a, b, Vector2((a.x + b.x) / 2.0, (a.y + b.y) / 2.0), p_left, p_right, p_player_solid)

# Three points: an arc, or a line when they are collinear.
static func arc(s: Vector2, e: Vector2, v: Vector2, p_left: SideSpec, p_right: SideSpec, p_player_solid := false) -> SurfaceSpec:
	var o := SurfaceSpec.new()
	o.start = s
	o.end = e
	o.via = v
	o.left = p_left
	o.right = p_right
	o.player_solid = p_player_solid
	return o

static func circle(c: Vector2, r: float, p_left: SideSpec, p_right: SideSpec, p_player_solid := false) -> SurfaceSpec:
	var o := SurfaceSpec.new()
	o.full_circle = true
	o.center = c
	o.radius = r
	o.left = p_left
	o.right = p_right
	o.player_solid = p_player_solid
	return o

func segment() -> Segment:
	if full_circle:
		return Segment.full_from_carrier(GeneralizedCircle.from_circle(center, radius))
	return Segment.from_coords(start, end, via)

func is_portal() -> bool:
	return resolver_table.is_empty() and left != null and EffectSpec.is_portal(left.effect)

func presence_conds() -> Array[PresenceCond]:
	var out: Array[PresenceCond] = []
	for c in presence:
		out.append(PresenceCond.new(c["key"], PackedStringArray(c["values"])))
	return out

# The surfaces (one; a portal pair is two: the source, then its image).
func build() -> Array[Surface]:
	var seg := segment()
	var out: Array[Surface] = []
	if is_portal():
		out = _build_portal(seg)
	elif not resolver_table.is_empty():
		var table := {}
		for k in resolver_table:
			var pair: Array = resolver_table[k]
			table[k] = [pair[0].build(seg.get_carrier()), pair[1].build(seg.get_carrier())]
		out = [Surface.with_resolver(seg, CategoricalResolver.new(resolver_key, table), false, player_solid)]
	else:
		var l := left.build(seg.get_carrier()) if left != null else null
		var r := right.build(seg.get_carrier()) if right != null else null
		out = [Surface.new(seg, l, r, false, player_solid)]
	var conds := presence_conds()
	# a host exists while every channel it spawns into is armed (GAME_SPEC 10.6)
	for ch in spawn_channels():
		conds.append(Channel.host_presence(ch))
	for s in out:
		s.presence.assign(conds)
	return out

# The channels the surface's sides spawn into (every table entry included).
func spawn_channels() -> PackedStringArray:
	var out := PackedStringArray()
	var sides: Array = [left, right]
	for k in resolver_table:
		sides.append_array(resolver_table[k])
	for s: SideSpec in sides:
		if s == null:
			continue
		for h in s.hits:
			if h.get("kind", "") == "spawn" and not out.has(h["channel"]):
				out.append(h["channel"])
	return out

# A portal (GAME_SPEC 10.2): the pair A -> B and its inverse, linked side to side.
func _build_portal(seg: Segment) -> Array[Surface]:
	var e: Dictionary = left.effect
	var res := RigidMotionEffect.create_portal_pair(seg, float(e["theta"]), EffectSpec.vec(e["d"]))
	var rs: SideSpec = right if right != null else left
	var src := Surface.new(seg, SideConfig.new(res.source_effect, left.interactive, HitEffectSpec.build_all(left.hits)),
		SideConfig.new(res.source_effect, rs.interactive, HitEffectSpec.build_all(rs.hits)), false, player_solid)
	var tgt := Surface.new(res.target_segment, SideConfig.new(res.target_effect, left.interactive, HitEffectSpec.build_all(left.hits)),
		SideConfig.new(res.target_effect, rs.interactive, HitEffectSpec.build_all(rs.hits)), false, player_solid)
	var link_l := SideLink.from_pair(src, Side.Value.LEFT, tgt, Side.Value.RIGHT)
	src.set_side_link(Side.Value.LEFT, link_l)
	tgt.set_side_link(Side.Value.RIGHT, link_l.outgoing)
	var link_r := SideLink.from_pair(src, Side.Value.RIGHT, tgt, Side.Value.LEFT)
	src.set_side_link(Side.Value.RIGHT, link_r)
	tgt.set_side_link(Side.Value.LEFT, link_r.outgoing)
	return [src, tgt]

# Problems with the spec ([] = valid).
func validate() -> Array[String]:
	var out: Array[String] = []
	if full_circle:
		if not (radius > 0.0):
			out.append("a full circle needs a positive radius")
			return out
	elif start == end:
		out.append("a segment needs distinct endpoints")
		return out
	var is_line := segment().is_line()
	var sides: Array = [left, right]
	for k in resolver_table:
		sides.append_array(resolver_table[k])
	for s in sides:
		if s != null:
			out.append_array(s.validate(is_line))
	if is_portal() and right != null and right.effect != left.effect:
		out.append("a portal's sides are the same portal")
	for c in presence:
		if not (c.has("key") and c.has("values")):
			out.append("a presence condition is {key, values}")
	return out

# The spec mapped through a rigid frame (rotation + translation, optionally a
# reflection). A reflecting frame reverses the segment's orientation, so the
# sides swap (the side facing a region keeps facing its image); a portal's
# motion is conjugated by the frame: T' = F T F^-1.
func mapped(xf: Transform2D) -> SurfaceSpec:
	var o := SurfaceSpec.new()
	o.full_circle = full_circle
	o.start = xf * start
	o.end = xf * end
	o.via = xf * via
	o.center = xf * center
	o.radius = radius
	var mirrored := xf.determinant() < 0.0
	var l := left.mapped(xf) if left != null else null
	var r := right.mapped(xf) if right != null else null
	o.left = r if mirrored else l
	o.right = l if mirrored else r
	if is_portal():
		var e: Dictionary = left.effect
		var theta := float(e["theta"])
		var d := EffectSpec.vec(e["d"])
		var at0: Vector2 = xf * RigidMotionEffect.apply_motion(xf.affine_inverse() * Vector2.ZERO, theta, d)
		for s in [o.left, o.right]:
			if s != null:
				s.effect["theta"] = -theta if mirrored else theta
				s.effect["d"] = [at0.x, at0.y]
	o.resolver_key = resolver_key
	for k in resolver_table:
		var pair: Array = resolver_table[k]
		var tl: SideSpec = pair[0].mapped(xf) if pair[0] != null else null
		var tr: SideSpec = pair[1].mapped(xf) if pair[1] != null else null
		o.resolver_table[k] = [tr, tl] if mirrored else [tl, tr]
	o.presence = presence.duplicate(true)
	o.player_solid = player_solid
	return o

# The spec of a surface (a portal's source gives its pair back).
static func of(s: Surface) -> SurfaceSpec:
	var o := SurfaceSpec.new()
	var carrier := s.segment.get_carrier()
	if s.segment.full and not carrier.is_line():
		o.full_circle = true
		o.center = Vector2(-carrier.b / (2.0 * carrier.a), -carrier.c / (2.0 * carrier.a))
		o.radius = carrier.radius()
	else:
		o.start = s.segment.start.coords
		o.end = s.segment.end.coords
		o.via = s.segment.via.coords
	if s.resolver is CategoricalResolver:
		o.resolver_key = s.resolver.state_key
		for k in s.resolver.table:
			o.resolver_table[k] = [SideSpec.of(s.resolver.table[k][0]), SideSpec.of(s.resolver.table[k][1])]
	else:
		o.left = SideSpec.of(s.active_side_config(Side.Value.LEFT, GameState.new()))
		o.right = SideSpec.of(s.active_side_config(Side.Value.RIGHT, GameState.new()))
	for c in s.presence:
		o.presence.append(c.to_def())
	o.player_solid = s.player_solid
	return o
