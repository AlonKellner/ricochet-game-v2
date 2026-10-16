class_name HitEffectSpec
extends RefCounted

# The hit-effect registry (Stage 107): a spec {"kind", "deferred", ...} ->
# HitEffect; HitEffect.to_spec() is the inverse.

static func build(spec: Dictionary) -> HitEffect:
	var deferred: bool = spec.get("deferred", false)
	match String(spec.get("kind", "")):
		"state":
			var op := StateChange.Op.TOGGLE if spec.get("op", "set") == "toggle" else StateChange.Op.SET
			return StateChangeHit.new(spec["key"], op, spec.get("value", true), deferred)
		"target":
			return TargetHit.new(deferred)
		"reset":
			return ResetHit.new(PackedStringArray(spec.get("channels", [])), deferred)
	push_error("HitEffectSpec.build: unknown hit effect kind '%s'" % spec.get("kind", ""))
	return null

static func build_all(specs: Array) -> Array[HitEffect]:
	var out: Array[HitEffect] = []
	for s in specs:
		var h := build(s)
		if h != null:
			out.append(h)
	return out
