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
	var landing: Array[String] = []
	var channels := spawns.keys()
	channels.sort()
	for ch: String in channels:
		if flags.get(Channel.key(ch)) != Channel.PENDING:
			continue   # cancelled in the shot (a reset after the spawn)
		var n: int = int(gens.get(ch, 0)) + 1
		gens[ch] = n
		flags[Channel.key(ch)] = Channel.generation_value(n)
		landing.append(ch)
	# trimming (Stage 108, GAME_SPEC 10.6.3) cuts against what is present once
	# the shot has landed: the world's present surfaces, then the groups this
	# landing placed before (so two new spawns never cross either)
	var cutters: Array[Segment] = []
	for s: Surface in by_id.values():
		if not s.pre_transform and s.present(flags):
			cutters.append(s.segment)
	var placed: Array = []
	for ch in landing:
		var rec: Array = spawns[ch]
		var spec := place(rec[2].template, rec[0], rec[1])
		if rec[2].template.trim:
			spec = trim(spec, cutters)
		spec.presence.append(Channel.group_presence(ch, gens[ch]).to_def())
		cutters.append(spec.segment())
		placed.append({"channel": ch, "generation": gens[ch], "spec": spec})
	return {"flags": flags, "placed": placed, "generations": gens}

# A placed surface trimmed to the piece of it that contains its midpoint (its
# via point; a full circle has none -- and a spawned circle is centred on the
# hit point, all of it equally near -- so it keeps its longest arc), split by
# its proper crossings with `cutters` (GAME_SPEC 10.6.3). No crossing: as it is.
static func trim(spec: SurfaceSpec, cutters: Array[Segment]) -> SurfaceSpec:
	if spec.is_portal():
		return spec   # a portal pair is not split (GAME_SPEC 9.7)
	var seg := spec.segment()
	var cuts: Array = []
	for c in cutters:
		for x in Crossings.of(seg, c):
			cuts.append([x["ta"], x["point"]])
	if cuts.is_empty() or (seg.full and cuts.size() < 2):
		return spec
	cuts.sort_custom(func(x, y): return x[0] < y[0])
	var carrier := seg.get_carrier()
	var a := seg.start.coords
	var b := seg.end.coords
	if seg.full:
		# the longest arc between consecutive cuts (cyclic)
		var best := -1.0
		for i in cuts.size():
			var t0: float = cuts[i][0]
			var t1: float = cuts[(i + 1) % cuts.size()][0] + (TAU if i + 1 == cuts.size() else 0.0)
			if t1 - t0 > best:
				best = t1 - t0
				a = cuts[i][1]
				b = cuts[(i + 1) % cuts.size()][1]
	else:
		var keep_t := Crossings.param(seg, seg.via.coords)
		for x in cuts:
			if x[0] <= keep_t:
				a = x[1]
			elif b == seg.end.coords:
				b = x[1]
	var out := spec.mapped(Transform2D.IDENTITY)
	out.full_circle = false
	out.start = a
	out.end = b
	out.via = Pieces._between(seg, carrier, a, b)
	out.straight = carrier.is_line()
	return out

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
