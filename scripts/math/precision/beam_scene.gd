class_name BeamScene
extends RefCounted

# A scene for the beam solver: segment surfaces with per-side effect types,
# at one precision tier. Built from scene defs, the JSON-able description of
# the game's surfaces (defs_from_surfaces; the same format the oracle traces,
# bench/scenes/*.surfaces.json):
#   {id, line, start, end, via, full, circle: {center, radius} | null,
#    left, right: {type, interactive, ...} | null, is_target, player_solid}
# Effect types: "" (pass), "terminal", "reflection", "inversion", "portal"
# (rigid motion z -> a z + d onto the paired surface, a = unit(cos θ, sin θ)
# of the float64 values; {theta, d, pair}).

const SUPPORTED_TYPES := ["pass", "terminal", "reflection", "inversion", "portal",
	"line_normal_projection", "circle_normal_projection", "line_directional_projection", "circle_directional_projection"]
const TRANSFORMATIVE := ["reflection", "inversion", "portal"]
const PROJECTIVE := ["line_normal_projection", "circle_normal_projection", "line_directional_projection",
	"circle_directional_projection"]

class Surf extends RefCounted:
	var id: int
	var index: int
	var seg: BeamGeom.Seg
	var left_type: String = ""
	var right_type: String = ""
	var is_target: bool = false
	# raw float64 geometry (FastSolver): line s -> e, or circle (cx, cy, r)
	var line: bool = true
	var sx: float
	var sy: float
	var ex: float
	var ey: float
	var cx: float
	var cy: float
	var r: float
	var r2: float
	# portal: rigid motion a z + d (a at the scene's tier, and its float64
	# twin pa + i pb), the paired surface id
	var portal: bool = false
	var alpha: CN = null
	var dvec: CN = null
	var pa: float = 1.0
	var pb: float = 0.0
	var dx: float = 0.0
	var dy: float = 0.0
	var theta: float = 0.0
	var pair: int = -1
	# projectives per side: [back, nx, ny] (normal for directional types)
	var left_proj: Array = []
	var right_proj: Array = []
	var has_v: bool = false   # via point (null for full lines: V at infinity)
	var vx: float
	var vy: float
	var full: bool = false

	# state changes per side ({key, op, value} or null), and the categorical
	# table (flag_key -> [left_type, right_type, left_state, right_state])
	var left_state = null
	var right_state = null
	var state_key: String = ""
	var table: Dictionary = {}

	# the table entry under the flags (StateChange.flag_key; "DEFAULT", then the
	# first entry), null for a fixed surface
	func _entry(flags: Dictionary):
		if state_key == "":
			return null
		if flags.has(state_key):
			var k := StateChange.flag_key(flags[state_key])
			if table.has(k):
				return table[k]
		if table.has("DEFAULT"):
			return table["DEFAULT"]
		return table.values()[0]

	func effect(left: bool, flags: Dictionary = {}) -> String:
		if _ghosted(left, flags):
			return ""
		var e = _entry(flags)
		if e != null:
			return e[0] if left else e[1]
		return left_type if left else right_type

	func state_change(left: bool, flags: Dictionary = {}):
		if _ghosted(left, flags):
			return null
		return _base_state(left, flags)

	func _base_state(left: bool, flags: Dictionary):
		var e = _entry(flags)
		if e != null:
			return e[2] if left else e[3]
		return left_state if left else right_state

	# Ghost mode (GAME_SPEC 10.6, Stage 86): while the "ghost" flag is true
	# every side but a ghost toggler (its state change writes "ghost") is a pass.
	func _ghosted(left: bool, flags: Dictionary) -> bool:
		if flags.get(GHOST) != true:
			return false
		var ch = _base_state(left, flags)
		return ch == null or String(ch.get("key", "")) != GHOST

	# Exact circle data (cached): homogeneous center [x, y, w], radius² times
	# w² (BigFloat), and their float64 roundings (for certified filters).
	var _exact_circle = null
	func exact_circle() -> Array:
		if _exact_circle == null:
			var C: Array
			var rw2: BigFloat
			if full:
				C = [Exact.of(cx), Exact.of(cy), Exact.of(1.0)]
				rw2 = Exact.mul(Exact.of(r), Exact.of(r))
			else:
				C = Exact.circumcenter_h(sx, sy, vx, vy, ex, ey)
				var dx := Exact.sub(Exact.mul(Exact.of(sx), C[2]), C[0])
				var dy := Exact.sub(Exact.mul(Exact.of(sy), C[2]), C[1])
				rw2 = Exact.add(Exact.mul(dx, dx), Exact.mul(dy, dy))
			var w: float = C[2].to_float()
			_exact_circle = [C, rw2, C[0].to_float() / w, C[1].to_float() / w, rw2.to_float() / (w * w)]
		return _exact_circle

var surfaces: Array = []
var by_id: Dictionary = {}

const CATEGORICAL_TYPES := ["", "terminal", "reflection", "inversion"]
# The reserved per-shot flag of ghost mode.
const GHOST := "ghost"

static func _state_of(cfg):
	return null if cfg == null else cfg.get("state")

static func _type_of(cfg) -> String:
	if cfg == null:
		return ""
	var t := String(cfg.get("type", "pass"))
	return "" if t == "pass" else t

static func from_defs(ctx: NumCtx, defs: Array) -> BeamScene:
	var sc := BeamScene.new()
	for d in defs:
		var s := Surf.new()
		s.id = int(d["id"])
		s.index = sc.surfaces.size()
		var g := {"id": s.id, "start": d["start"], "end": d["end"], "via": d.get("via"), "full": bool(d.get("full", false))}
		if bool(d.get("line", false)):
			g["kind"] = "line"
		elif d.get("circle") != null:
			g["kind"] = "full_circle"
			g["center"] = d["circle"]["center"]
			g["radius"] = d["circle"]["radius"]
		else:
			g["kind"] = "arc"
		s.seg = BeamGeom.seg_from_dict(ctx, g)
		s.line = s.seg.kind == BeamGeom.Kind.LINE
		s.full = s.seg.full
		if s.seg.v != null:
			s.has_v = true
			s.vx = s.seg.v.fx()
			s.vy = s.seg.v.fy()
		s.sx = float(d["start"][0])
		s.sy = float(d["start"][1])
		s.ex = float(d["end"][0])
		s.ey = float(d["end"][1])
		if not s.line:
			# raw circle: the tier's carrier rounded to float64 (exact for
			# full circles, whose dumped center/radius are float64)
			s.cx = s.seg.circ.c.fx()
			s.cy = s.seg.circ.c.fy()
			s.r = s.seg.circ.r.to_float()
			s.r2 = s.r * s.r
		s.left_type = _type_of(d.get("left"))
		s.right_type = _type_of(d.get("right"))
		s.left_state = _state_of(d.get("left"))
		s.right_state = _state_of(d.get("right"))
		var res = d.get("resolver")
		if res != null:
			s.state_key = String(res["key"])
			for k in res["table"]:
				var en: Dictionary = res["table"][k]
				for c in [en.get("left"), en.get("right")]:
					assert(_type_of(c) in CATEGORICAL_TYPES, "categorical sides: pass / terminal / reflection / inversion")
				s.table[String(k)] = [_type_of(en.get("left")), _type_of(en.get("right")), _state_of(en.get("left")), _state_of(en.get("right"))]
		for key in ["left", "right"]:
			var pc = d.get(key)
			if pc != null and String(pc.get("type", "")) in PROJECTIVE:
				var nrm = pc.get("normal")
				var pr := [bool(pc.get("back", false)), 0.0 if nrm == null else float(nrm[0]), 0.0 if nrm == null else float(nrm[1])]
				if key == "left":
					s.left_proj = pr
				else:
					s.right_proj = pr
		for key in ["left", "right"]:
			var cfg = d.get(key)
			if cfg != null and String(cfg.get("type", "")) == "portal":
				s.portal = true
				s.theta = float(cfg["theta"])
				s.dx = float(cfg["d"][0])
				s.dy = float(cfg["d"][1])
				s.pair = int(cfg["pair"])
				s.alpha = ctx.cn(cos(s.theta), sin(s.theta)).unit()
				s.dvec = ctx.cn(s.dx, s.dy)
				var c := cos(s.theta)
				var sn := sin(s.theta)
				var ab := sqrt(c * c + sn * sn)
				s.pa = c / ab
				s.pb = sn / ab
		s.is_target = bool(d.get("is_target", false))
		sc.surfaces.append(s)
		sc.by_id[s.id] = s
	return sc

# True if every side config is one the beam solver implements.
static func supported(defs: Array) -> bool:
	for d in defs:
		var res = d.get("resolver")
		if res != null:
			for k in res["table"]:
				for c in [res["table"][k].get("left"), res["table"][k].get("right")]:
					if not (_type_of(c) in CATEGORICAL_TYPES):
						return false
		for key in ["left", "right"]:
			var cfg = d.get(key)
			if cfg != null and not (String(cfg.get("type", "")) in SUPPORTED_TYPES):
				return false
			# a portal needs its paired surface (the beam departs from it)
			if cfg != null and String(cfg.get("type", "")) == "portal" and cfg.get("pair") == null:
				return false
	return true

# ---------------------------------------------------------------- game surfaces

static func defs_from_surfaces(game_surfaces: Array) -> Array:
	var out: Array = []
	for s in game_surfaces:
		out.append(describe_surface(s))
	# portal pairs: the surface whose transform is this one's inverse
	var state := GameState.new()
	for i in game_surfaces.size():
		for key in ["left", "right"]:
			var cfg = out[i][key]
			if cfg == null or cfg["type"] != "portal":
				continue
			var e: Effect = game_surfaces[i].active_side_config(Side.Value.LEFT if key == "left" else Side.Value.RIGHT, state).effect
			for j in game_surfaces.size():
				var c2: SideConfig = game_surfaces[j].active_side_config(Side.Value.LEFT, state)
				if j != i and c2 != null and c2.effect != null and c2.effect == e.partner():
					cfg["pair"] = game_surfaces[j].id
					break
	return out

static func _v(p: Vector2):
	if is_inf(p.x) or is_inf(p.y):
		return null
	return [p.x, p.y]

static func describe_surface(s: Surface) -> Dictionary:
	var state := GameState.new()
	var carrier := s.segment.get_carrier()
	var circle = null
	if s.segment.full and not carrier.is_line():
		# exact carrier of a full circle (float64): center and radius
		circle = {"center": [-carrier.b / (2.0 * carrier.a), -carrier.c / (2.0 * carrier.a)], "radius": carrier.radius()}
	var out := {
		"circle": circle,
		"id": s.id,
		"start": _v(s.segment.start.coords),
		"end": _v(s.segment.end.coords),
		"via": _v(s.segment.via.coords),
		"full": s.segment.full,
		"line": carrier.is_line(),
		"left": null if s.resolver is CategoricalResolver else describe_config(s.active_side_config(Side.Value.LEFT, state)),
		"right": null if s.resolver is CategoricalResolver else describe_config(s.active_side_config(Side.Value.RIGHT, state)),
		"is_target": s.is_target,
		"player_solid": s.player_solid,
	}
	if s.resolver is CategoricalResolver:
		out["resolver"] = describe_resolver(s.resolver)
	return out

# A categorical resolver as scene defs: {key, table: {flag_key: {left, right}}}
# (null for a fixed surface).
static func describe_resolver(r: ConfigResolver):
	if not (r is CategoricalResolver):
		return null
	var table := {}
	for k in r.table:
		table[k] = {"left": describe_config(r.table[k][0]), "right": describe_config(r.table[k][1])}
	return {"key": r.state_key, "table": table}

static func describe_config(cfg: SideConfig):
	if cfg == null:
		return null
	var e: Effect = cfg.effect
	var d := {"interactive": cfg.interactive}
	if e == null:
		d["type"] = "pass"
	elif e is TerminalEffect:
		d["type"] = "terminal"
	elif e is CircleInversionEffect:
		d["type"] = "inversion"
	elif e is ReflectionEffect:
		d["type"] = "reflection"
	elif e is RigidMotionEffect:
		d["type"] = "portal"
		d["theta"] = e._theta
		d["d"] = [e._d.x, e._d.y]
	elif e is LineNormalProjection:
		d["type"] = "line_normal_projection"
		d["back"] = e._project_back
	elif e is CircleNormalProjection:
		d["type"] = "circle_normal_projection"
		d["back"] = e._project_back
	elif e is LineDirectionalProjection:
		d["type"] = "line_directional_projection"
		d["back"] = e._project_back
		d["normal"] = [e._normal.x, e._normal.y]
	elif e is CircleDirectionalProjection:
		d["type"] = "circle_directional_projection"
		d["back"] = e._project_back
		d["normal"] = [e._normal.x, e._normal.y]
	else:
		d["type"] = "unknown:" + str(e.get_script().get_global_name())
	if cfg.state_change != null:
		d["state"] = cfg.state_change.to_def()
	return d
