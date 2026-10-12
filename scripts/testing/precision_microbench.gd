extends SceneTree

# Headless: godot --headless --path . -s res://scripts/testing/precision_microbench.gd
# Checks BigFloat against mpmath fixtures and reports ns/op per precision.

func _init() -> void:
	_check_correctness()
	if not OS.get_cmdline_user_args().has("--no-bench"):
		_bench()
	quit()

static func sci(v: float) -> String:
	if v == 0.0:
		return "0"
	var e := int(floor(log(absf(v)) / log(10.0)))
	return "%.2fe%d" % [v / pow(10.0, e), e]

func _rel_err(x: BigFloat, ref: BigFloat) -> float:
	if ref.is_zero():
		return x.abs_val().to_float()
	var d := x.sub(ref)
	if d.is_zero():
		return 0.0
	return absf(d.to_float() / ref.to_float())

func _check_correctness() -> void:
	var f := FileAccess.open("res://bench/fixtures/bigfloat_cases.json", FileAccess.READ)
	var cases: Array = JSON.parse_string(f.get_as_text())
	for n in [4, 8, 16]:
		var worst := {"add": 0.0, "mul": 0.0, "div": 0.0, "sqrt": 0.0}
		for c in cases:
			var ref_n: int = n + 4
			var a := BigFloat.from_string(c["a"], n)
			var b := BigFloat.from_string(c["b"], n)
			# addition error is measured relative to the operands (standard for sums)
			var add_ref := BigFloat.from_string(c["add"], ref_n)
			var add_err := a.add(b).with_limbs(ref_n).sub(add_ref).abs_val().to_float()
			var scale := maxf(absf(a.to_float()), absf(b.to_float()))
			worst["add"] = maxf(worst["add"], add_err / scale)
			worst["mul"] = maxf(worst["mul"], _rel_err(a.mul(b).with_limbs(ref_n), BigFloat.from_string(c["mul"], ref_n)))
			worst["div"] = maxf(worst["div"], _rel_err(a.div(b).with_limbs(ref_n), BigFloat.from_string(c["div"], ref_n)))
			worst["sqrt"] = maxf(worst["sqrt"], _rel_err(a.abs_val().sqrt_val().with_limbs(ref_n), BigFloat.from_string(c["sqrt_abs_a"], ref_n)))
		print("limbs=%d (%d bits): worst rel err add=%s mul=%s div=%s sqrt=%s  (2^-bits=%s)" % [
			n, n * BigFloat.BITS, sci(worst["add"]), sci(worst["mul"]), sci(worst["div"]), sci(worst["sqrt"]), sci(pow(2.0, -n * BigFloat.BITS))])

func _bench() -> void:
	var reps := 20000
	# f64 baseline
	var x := 1.000001
	var y := 0.999999
	var t0 := Time.get_ticks_usec()
	var acc := 0.0
	for i in reps:
		acc += x * y + x
	var t_f64 := float(Time.get_ticks_usec() - t0) * 1000.0 / reps
	print("f64 mul+add: %.0f ns/iter (acc=%f)" % [t_f64, acc])
	for n in [2, 4, 8, 16]:
		var a := BigFloat.from_float(1.2345678901234567, n)
		var b := BigFloat.from_float(-0.7654321098765432, n)
		var r := a
		t0 = Time.get_ticks_usec()
		for i in reps:
			r = a.add(b)
		var t_add := float(Time.get_ticks_usec() - t0) * 1000.0 / reps
		t0 = Time.get_ticks_usec()
		for i in reps:
			r = a.mul(b)
		var t_mul := float(Time.get_ticks_usec() - t0) * 1000.0 / reps
		var reps_d := reps / 10
		t0 = Time.get_ticks_usec()
		for i in reps_d:
			r = a.div(b)
		var t_div := float(Time.get_ticks_usec() - t0) * 1000.0 / reps_d
		t0 = Time.get_ticks_usec()
		for i in reps_d:
			r = a.sqrt_val()
		var t_sqrt := float(Time.get_ticks_usec() - t0) * 1000.0 / reps_d
		print("BigFloat %2d limbs (%3d bits): add %6.0f ns  mul %6.0f ns  div %7.0f ns  sqrt %7.0f ns" % [n, n * BigFloat.BITS, t_add, t_mul, t_div, t_sqrt])
