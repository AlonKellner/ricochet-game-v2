extends SceneTree

# Headless benchmark runner:
#   godot --headless --path . -s res://scripts/testing/run_precision_bench.gd -- [--filter=substr] [--strategies=a,b] [--repeat=N]
# Writes bench/results/gd_results.json and prints a pass-rate table by depth.

var BUCKETS := [[1, 3], [4, 6], [7, 9], [10, 12], [13, 15]]

func _init() -> void:
	var args := {}
	for a in OS.get_cmdline_user_args():
		var kv := a.trim_prefix("--").split("=")
		args[kv[0]] = kv[1] if kv.size() > 1 else "1"
	var filter: String = args.get("filter", "")
	PrecisionBench.use_corpus(args.get("dir", ""))
	var strategies: Array = String(args.get("strategies", "beam_f64,beam_big112,ladder")).split(",")
	if args.get("dir", "") == "stress":
		BUCKETS = [[16, 17], [18, 19], [20, 21], [22, 23], [24, 25]]
	var names := PrecisionBench.list_scenarios(filter)
	var results: Array = []
	for name in names:
		var scn: Dictionary = PrecisionBench.load_json("%s/%s.json" % [PrecisionBench.SCN_DIR, name])
		var exp: Dictionary = PrecisionBench.load_json("%s/%s.json" % [PrecisionBench.EXP_DIR, name])
		var n_pl: int = exp["planned"].size()
		var n_ph: int = exp["physical"].size()
		for strat in strategies:
			var t0 := Time.get_ticks_usec()
			var out: Dictionary
			match strat:
				"beam_f64":
					out = PrecisionBench.run_beam(scn, NumCtx.f64(), n_pl, n_ph)
				"ladder":
					out = PrecisionBench.run_ladder(scn, n_ph)
				"ladder_verify":
					out = PrecisionBench.run_ladder(scn, n_ph, true)
				"est_f64":
					out = PrecisionBench.run_estimated_f64(scn, n_ph)
				_:
					if strat.begins_with("beam_big"):
						var bits := int(strat.trim_prefix("beam_big"))
						out = PrecisionBench.run_beam(scn, NumCtx.with_limbs(BigFloat.limbs_for_bits(bits)), n_pl, n_ph)
			var usec := Time.get_ticks_usec() - t0
			for kind in ["planned", "physical"]:
				var cmp := PrecisionBench.compare(out[kind], exp[kind])
				results.append({"scenario": name, "depth": scn["plan"].size(), "strategy": strat, "trace": kind,
					"ok": cmp["ok"], "max_err": cmp["max_err"], "mismatch": cmp["mismatch"],
					"matched": cmp["matched"], "total": cmp["total"], "usec": usec,
					"max_sens": exp.get("max_sens_to_cursor", -1.0), "tier": out.get("tier", -1),
					"converged": out.get("converged", true), "calib": cmp["calib"],
					"max_gain": out.get("max_gain", -1.0), "tiers_tried": out.get("tiers_tried", []),
					"usec_planned": out.get("usec_planned", -1), "usec_physical": out.get("usec_physical", -1)})
	var f := FileAccess.open("res://bench/results/gd_results_%s.json" % args.get("tag", "all"), FileAccess.WRITE)
	f.store_string(JSON.stringify(results, " "))
	f.close()
	_print_table(results, strategies)
	quit()

func _print_table(results: Array, strategies: Array) -> void:
	var header := "%-14s %-8s" % ["strategy", "trace"]
	for b in BUCKETS:
		header += "  d%2d-%-2d " % [b[0], b[1]]
	header += "   all     median_ms  max_ms"
	print(header)
	for strat in strategies:
		for kind in ["planned", "physical"]:
			var rows := results.filter(func(r): return r["strategy"] == strat and r["trace"] == kind)
			var line := "%-14s %-8s" % [strat, kind]
			for b in BUCKETS:
				var rr := rows.filter(func(r): return r["depth"] >= b[0] and r["depth"] <= b[1])
				var ok := rr.filter(func(r): return r["ok"]).size()
				line += "  %3d/%-3d " % [ok, rr.size()]
			var all_ok := rows.filter(func(r): return r["ok"]).size()
			var times: Array = rows.map(func(r): return r["usec"] / 1000.0)
			times.sort()
			var med: float = times[times.size() / 2] if not times.is_empty() else 0.0
			var mx: float = times.back() if not times.is_empty() else 0.0
			line += "  %3d/%-3d  %8.2f  %7.1f" % [all_ok, rows.size(), med, mx]
			print(line)
