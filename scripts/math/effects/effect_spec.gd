class_name EffectSpec
extends RefCounted

# The path-effect registry (Stage 107): one row per scene-def `type`, with the
# carrier it needs and how to build it. Effect.to_spec() is the inverse of
# build(). Adding a kind is one Effect class and one row here (plus the
# solver's native type).

enum Carrier { ANY, LINE, CIRCLE }

const CARRIER := {
	"pass": Carrier.ANY,
	"terminal": Carrier.ANY,
	"reflection": Carrier.ANY,
	"inversion": Carrier.CIRCLE,
	"portal": Carrier.ANY,
	"line_normal_projection": Carrier.LINE,
	"circle_normal_projection": Carrier.CIRCLE,
	"line_directional_projection": Carrier.LINE,
	"circle_directional_projection": Carrier.CIRCLE,
}

static func types() -> Array:
	return CARRIER.keys()

# Problems with a spec on a carrier ([] = valid).
# carrier_dir: a line carrier's direction (end - start), for the rule that a
# directional projection's normal must leave its line (GAME_SPEC 23, B9).
static func validate(spec: Dictionary, carrier_is_line: bool, carrier_dir: Vector2 = Vector2.ZERO) -> Array[String]:
	var out: Array[String] = []
	var t: String = spec.get("type", "pass")
	if not CARRIER.has(t):
		out.append("unknown effect type '%s'" % t)
		return out
	var need: int = CARRIER[t]
	if need == Carrier.LINE and not carrier_is_line:
		out.append("%s needs a line carrier" % t)
	if need == Carrier.CIRCLE and carrier_is_line:
		out.append("%s needs a circle carrier" % t)
	if t.ends_with("directional_projection") and not spec.has("normal"):
		out.append("%s needs a normal" % t)
	if t == "line_directional_projection" and spec.has("normal") and carrier_dir != Vector2.ZERO \
			and vec(spec["normal"]).cross(carrier_dir) == 0.0:
		out.append("%s: the normal runs along the line carrier, no beam leaves it (GAME_SPEC 23)" % t)
	if t == "portal" and not (spec.has("theta") and spec.has("d")):
		out.append("a portal needs theta and d")
	return out

# The effect of a spec on a carrier (null for the pass). A portal is built by
# RigidMotionEffect.create_portal_pair (it needs the segment: SurfaceSpec).
static func build(spec: Dictionary, carrier: GeneralizedCircle) -> Effect:
	var t: String = spec.get("type", "pass")
	var back: bool = spec.get("back", false)
	match t:
		"pass":
			return null
		"terminal":
			return TerminalEffect.new()
		"reflection":
			return ReflectionEffect.new(carrier)
		"inversion":
			return CircleInversionEffect.new(carrier)
		"line_normal_projection":
			return LineNormalProjection.new(back)
		"circle_normal_projection":
			return CircleNormalProjection.new(back)
		"line_directional_projection":
			return LineDirectionalProjection.new(vec(spec["normal"]), back)
		"circle_directional_projection":
			return CircleDirectionalProjection.new(vec(spec["normal"]), back)
	assert(t != "portal", "EffectSpec.build: a portal is built as a pair (SurfaceSpec)")
	push_error("EffectSpec.build: unknown effect type '%s'" % t)
	return null

static func is_portal(spec: Dictionary) -> bool:
	return spec.get("type", "pass") == "portal"

# [x, y] or a Vector2 -> Vector2.
static func vec(v) -> Vector2:
	return v if v is Vector2 else Vector2(v[0], v[1])
