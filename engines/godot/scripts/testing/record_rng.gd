extends SceneTree

# Stage 116.6: Godot's RandomNumberGenerator as the engine computes it, for the
# native port (rcg::GodotRng) to pin -- the sweep positions' fuzz is drawn from
# it (SweepRunner.build_positions). Per seed: raw randi(), randf(), and
# randf_range over bounds pairs alternating as the sweeps draw x then y.
# Floats as "f" + their float64 bytes (HexJson, hex_json.gd).
#
#   godot --headless --path engines/godot -s res://scripts/testing/record_rng.gd -- "$PWD/tests/data/godot_rng.json"

const SEEDS := [0, 1, 7, 42, 87, 88, 103, 108, 110, 111, 112, 113, 114, 116, 209, 4294967295, 9223372036854775807]
const BOUNDS := [[0.0, 1920.0], [0.0, 1080.0], [210.0, 1690.0], [110.0, 890.0], [-3.5, 2.25]]

func _initialize() -> void:
	var cases := []
	for s in SEEDS:
		var a := RandomNumberGenerator.new()
		a.seed = s
		var randi_v := []
		for i in 24:
			randi_v.append(a.randi())
		var b := RandomNumberGenerator.new()
		b.seed = s
		var randf_v := []
		for i in 24:
			randf_v.append(b.randf())
		var c := RandomNumberGenerator.new()
		c.seed = s
		var range_v := []
		for i in 48:
			var bounds: Array = BOUNDS[i % BOUNDS.size()]
			range_v.append(c.randf_range(bounds[0], bounds[1]))
		cases.append({"seed": s, "randi": randi_v, "randf": randf_v, "randf_range": range_v})
	var f := FileAccess.open(OS.get_cmdline_user_args()[0], FileAccess.WRITE)
	f.store_string(HexJson.encode({"schema": "godot-rng/1", "engine": Engine.get_version_info()["string"], "bounds": BOUNDS, "cases": cases}) + "\n")
	f.close()
	print("recorded %d seeds" % SEEDS.size())
	quit()
