class_name SideSpec
extends Resource

# One side as data (Stage 107): its path effect (an EffectSpec dictionary, the
# scene-def form: {"type", "back", "normal", "theta", "d"}), its hit effects
# (HitEffectSpec dictionaries, in order) and whether it is plannable.

@export var effect: Dictionary = {"type": "pass"}
@export var hits: Array[Dictionary] = []
@export var interactive := false

static func make(p_effect: Dictionary = {"type": "pass"}, p_interactive := false, p_hits: Array = []) -> SideSpec:
	var s := SideSpec.new()
	s.effect = p_effect
	s.interactive = p_interactive
	s.hits.assign(p_hits)
	return s

# The side config on a carrier (a portal is built by SurfaceSpec, as a pair).
func build(carrier: GeneralizedCircle) -> SideConfig:
	return SideConfig.new(EffectSpec.build(effect, carrier), interactive, HitEffectSpec.build_all(hits))

static func of(cfg: SideConfig) -> SideSpec:
	if cfg == null:
		return null
	return make(cfg.effect.to_spec() if cfg.effect != null else {"type": "pass"}, cfg.interactive,
		cfg.hit_effects.map(func(h: HitEffect): return h.to_spec()))

func validate(carrier_is_line: bool) -> Array[String]:
	var out := EffectSpec.validate(effect, carrier_is_line)
	for h in hits:
		if HitEffectSpec.build(h) == null:
			out.append("unknown hit effect %s" % str(h))
		elif h.get("kind", "") == "spawn":
			if String(h.get("channel", "")).is_empty():
				out.append("a spawn needs a channel")
			if h.get("template") == null:
				out.append("a spawn needs a template")
			else:
				out.append_array(h["template"].validate())
	if effect.get("type", "pass") == "pass" and hits.is_empty() and interactive:
		out.append("a pass side without hit effects has nothing to plan (GAME_SPEC 9.1)")
	return out

# A copy with its direction-valued parameters mapped by a frame's basis (a
# directional projection's normal).
func mapped(xf: Transform2D) -> SideSpec:
	var e := effect.duplicate(true)
	if e.has("normal"):
		var n := xf.basis_xform(EffectSpec.vec(e["normal"]))
		e["normal"] = [n.x, n.y]
	return make(e, interactive, hits.duplicate(true))
