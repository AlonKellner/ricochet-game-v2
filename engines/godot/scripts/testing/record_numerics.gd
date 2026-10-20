extends SceneTree

# Stage 116.2: the game's float32 numerics as the engine computes them, for the
# native port to pin (tests/data/godot_xf2.json; rcg_tests): Transform2D
# (the rotation constructor, xform, basis_xform, *, affine_inverse,
# determinant) and the vector expressions of the spawn frame, on seeded random
# inputs. Floats are written as "f" + their float64 bytes (SessionDigest);
# Godot never prints or parses a number of this file.
#
#   godot --headless --path . -s res://scripts/testing/record_numerics.gd -- tests/data/godot_xf2.json

const N := 200

func _initialize() -> void:
	var out_path: String = OS.get_cmdline_user_args()[0]
	var rng := RandomNumberGenerator.new()
	rng.seed = 116
	var v := func() -> Vector2: return Vector2(rng.randf_range(-2000, 2000), rng.randf_range(-2000, 2000))
	var unit := func() -> Vector2: return Vector2.from_angle(rng.randf_range(-PI, PI))
	var cases := []
	for i in N:
		var rot := rng.randf_range(-7.0, 7.0)
		var pos: Vector2 = v.call()
		var a := Transform2D(rot, pos)
		# a general transform: random columns (scaled, skewed, reflected half the time)
		var b := Transform2D(v.call() / 1000.0, v.call() / 1000.0, v.call())
		var u: Vector2 = unit.call()
		var w: Vector2 = unit.call()
		var p: Vector2 = v.call()
		cases.append({
			"rot": rot, "pos": pos, "a": _xf(a), "b": _xf(b), "p": p, "u": u, "w": w,
			"a_xform_p": a * p, "b_xform_p": b * p, "b_basis_p": b.basis_xform(p),
			"det_b": b.determinant(), "a_mul_b": _xf(a * b), "b_mul_a": _xf(b * a),
			"inv_b": _xf(b.affine_inverse()), "inv_a": _xf(a.affine_inverse()),
			"frame": _xf(Transform2D(u, Vector2(-u.y, u.x), p) * Transform2D(0.0, p / 100.0)),
			"reflect": 2.0 * u.dot(w) * w - u,
			"mid": (p + pos) / 2.0,
			"norm": (p - pos).normalized(),
			"cross": (p - pos).cross(u),
		})
	var f := FileAccess.open(out_path, FileAccess.WRITE)
	f.store_string(SessionDigest.encode({"schema": "godot-numerics/1", "engine": Engine.get_version_info()["string"], "cases": cases}) + "\n")
	f.close()
	print("recorded %d cases" % N)
	quit()

func _xf(t: Transform2D) -> Array:
	return [t.x, t.y, t.origin]
