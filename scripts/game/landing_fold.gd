class_name LandingFold
extends RefCounted

# What a shot does when the arrow lands (GAME_SPEC 10.6): a pure fold over the
# physical trace's hit records (TracePath.hits). For every hit whose side
# FIRED, the side config resolved under the flags before that hit (the
# solver's own provenance) runs its landing parts in list order: deferred
# writes apply on top of the shot's final flags; a spawn remembers the hit
# that last fired it. A channel still "pending" then lands: its template is
# placed at that hit, as the channel's next generation. Nothing here touches
# the solver or the world; the same fold runs for the preview.

# hits: TracePath.hits; by_id: surface id -> Surface (the world the shot was
# traced in); final_flags: the shot's flags at its end; generations: channel
# -> the last generation landed.
# -> {"flags": the flags to promote, "placed": [{channel, generation, spec}],
#     "generations": the updated counters}
static func fold(hits: Array, by_id: Dictionary, final_flags: Dictionary, generations: Dictionary) -> Dictionary:
	var flags := final_flags.duplicate(true)
	var spawns := {}   # channel -> [host Surface, hit record, SpawnHit]
	for h: Dictionary in hits:
		if not h["fired"]:
			continue
		var host: Surface = by_id.get(int(h["surface_id"]))
		if host == null:
			continue
		var side: Side.Value = Side.Value.LEFT if int(h["side"]) == Side.Value.LEFT else Side.Value.RIGHT
		var cfg := host.active_side_config(side, GameState.new(h["flags_before"]))
		if cfg == null:
			continue
		for eff: HitEffect in cfg.hit_effects:
			for w in eff.landing_writes(host.id):
				flags = w.apply(flags)
			if eff is SpawnHit:
				spawns[eff.channel] = [host, h, eff]
	var gens := generations.duplicate()
	var placed: Array = []
	var channels := spawns.keys()
	channels.sort()
	for ch: String in channels:
		if flags.get(Channel.key(ch)) != Channel.PENDING:
			continue   # cancelled in the shot (a reset after the spawn)
		var n: int = int(gens.get(ch, 0)) + 1
		gens[ch] = n
		flags[Channel.key(ch)] = Channel.generation_value(n)
		var rec: Array = spawns[ch]
		var spec := place(rec[2].template, rec[0], rec[1])
		spec.presence.append(Channel.group_presence(ch, n).to_def())
		placed.append({"channel": ch, "generation": n, "spec": spec})
	return {"flags": flags, "placed": placed, "generations": gens}

# A template placed at a hit on its host (GAME_SPEC 10.6.1).
static func place(template: SpawnTemplate, host: Surface, hit: Dictionary) -> SurfaceSpec:
	var seg := host.segment
	var carrier := seg.get_carrier()
	var is_line := carrier.is_line()
	var point: Vector2 = hit["point"]
	var tangent := SpawnFrame.carrier_tangent(seg.start.coords, seg.via.coords, seg.end.coords, is_line,
		Vector2.ZERO if is_line else carrier.center(), point)
	var frame := SpawnFrame.frame(template.orientation, point, hit["tangent"], tangent,
		int(hit["side"]) == Side.Value.RIGHT, template.mirror_with_side)
	return template.place(frame)
