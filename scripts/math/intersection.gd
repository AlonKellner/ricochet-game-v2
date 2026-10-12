class_name Intersection
extends RefCounted

# Alias: hit records are TraceHit.
const HitRecord = preload("res://scripts/math/trace_hit.gd")

static func find_all_hits(ray: Ray, segments: Array, origin_on_seg: Segment = null, origin_carrier: GeneralizedCircle = null, frame: MobiusTransform = null, visual_carriers: Dictionary = {}) -> Array:
	var results: Array = []
	for seg in segments:
		var carrier_override: GeneralizedCircle = null
		if seg == origin_on_seg and origin_carrier != null:
			carrier_override = origin_carrier
		var vis_carrier: GeneralizedCircle = visual_carriers.get(seg) as GeneralizedCircle
		results.append_array(_find_segment_hits(ray, seg, seg == origin_on_seg, carrier_override, vis_carrier, frame))
	return results

static func _detect_endpoints_on_ray(ray: Ray, segment: Segment) -> Dictionary:
	if segment.full:
		return {}
	var result: Dictionary = {}
	var dir := ray.direction.to_vector()
	var ray_defining: Array[Vector2] = [ray.origin.coords, ray.direction.start.coords, ray.direction.end.coords]

	for ep_idx in [1, 2]:
		var ep_coords: Vector2 = segment.start.coords if ep_idx == 1 else segment.end.coords
		var detected := false
		for rp in ray_defining:
			if ep_coords == rp:
				detected = true
				break
		if not detected:
			var cross := (ep_coords - ray.origin.coords).cross(dir)
			if cross == 0.0:
				detected = true
		if detected:
			result[ep_idx] = project_point_on_ray(ray, ep_coords)
	return result

static func _find_segment_hits(ray: Ray, segment: Segment, origin_on_carrier: bool = false, carrier_override: GeneralizedCircle = null, visual_carrier: GeneralizedCircle = null, frame: MobiusTransform = null) -> Array:
	var ep_on_ray := _detect_endpoints_on_ray(ray, segment)
	var carrier: GeneralizedCircle = carrier_override if carrier_override != null else segment.get_carrier()
	var quad_hits: Array
	var use_pullback := false
	if visual_carrier != null and frame != null and frame.id != MobiusTransform.IDENTITY_ID and not visual_carrier.is_line() and not carrier.is_line() and carrier != visual_carrier:
		var norm_r := carrier.radius()
		use_pullback = is_nan(norm_r) or norm_r < 1.0
	if use_pullback:
		quad_hits = inversive_pullback_intersect(ray, visual_carrier, frame)
	else:
		quad_hits = _intersect_ray_carrier(ray, carrier)

	if quad_hits.is_empty() and ep_on_ray.is_empty() and not origin_on_carrier:
		return []

	if origin_on_carrier:
		if not quad_hits.is_empty():
			var best_idx := 0
			var best_abs_t := absf(quad_hits[0]["t"])
			for i in range(1, quad_hits.size()):
				var abs_t: float = absf(quad_hits[i]["t"])
				if abs_t < best_abs_t:
					best_abs_t = abs_t
					best_idx = i
			quad_hits[best_idx] = {"t": 0.0, "point": ray.origin.coords}
		else:
			quad_hits.append({"t": 0.0, "point": ray.origin.coords})

	var available_eps := ep_on_ray.duplicate()
	var results: Array = []

	for hit_dict in quad_hits:
		var t: float = hit_dict["t"]
		var point: Vector2 = hit_dict["point"]
		var ep := 0

		if not available_eps.is_empty():
			var best_ep := 0
			var best_dist := INF
			for ep_idx in available_eps:
				var dist := absf(t - float(available_eps[ep_idx]))
				if dist < best_dist:
					best_dist = dist
					best_ep = ep_idx
			if best_ep > 0:
				point = segment.start.coords if best_ep == 1 else segment.end.coords
				t = float(available_eps[best_ep])
				ep = best_ep
				available_eps.erase(best_ep)

		results.append(_build_hit_record(t, point, segment, ray, ep))

	for ep_idx in available_eps:
		var ep_coords: Vector2 = segment.start.coords if ep_idx == 1 else segment.end.coords
		var t: float = float(available_eps[ep_idx])
		results.append(_build_hit_record(t, ep_coords, segment, ray, ep_idx))

	return results

static func _build_hit_record(t: float, point: Vector2, segment: Segment, ray: Ray, ep: int) -> HitRecord:
	var on_seg := is_on_segment(point, segment)
	var side := _determine_side(ray, point, segment)
	var bl := false
	var br := false
	if on_seg:
		if ep == 0:
			bl = true
			br = true
		else:
			var sides := endpoint_blocked_sides(point, segment, ray, ep)
			bl = sides[0]
			br = sides[1]
	return HitRecord.new(t, point, segment, side, on_seg, ep, bl, br)

static func projective_sort(hits: Array) -> Array:
	var sorted := hits.duplicate()
	sorted.sort_custom(func(a: HitRecord, b: HitRecord) -> bool:
		var a_zero := a.t == 0.0
		var b_zero := b.t == 0.0
		if a_zero and b_zero:
			return false
		if a_zero:
			return false
		if b_zero:
			return true
		var a_pos := a.t > 0.0
		var b_pos := b.t > 0.0
		if a_pos and not b_pos:
			return true
		if not a_pos and b_pos:
			return false
		if a.t == b.t:
			var a_null := a.segment == null
			var b_null := b.segment == null
			if a_null != b_null:
				return a_null
			if a_null:
				return false
			return a.segment.get_instance_id() < b.segment.get_instance_id()
		return a.t < b.t
	)
	return sorted

static func intersect_line_with_carrier(ray: Ray, carrier: GeneralizedCircle) -> Array:
	return _intersect_ray_carrier(ray, carrier)

# Cross-ratio containment: P is on arc S→V→E iff Re(cross_ratio(S,P;E,V)) >= 0
static func is_on_segment(point: Vector2, segment: Segment) -> bool:
	if segment.full:
		return true
	var zP := Vector2(point.x, point.y)
	var wP := 1.0
	var zS := Vector2(segment.start.coords.x, segment.start.coords.y)
	var wS := 1.0
	var zE := Vector2(segment.end.coords.x, segment.end.coords.y)
	var wE := 1.0
	var zV: Vector2
	var wV: float
	if is_inf(segment.via.coords.x) or is_inf(segment.via.coords.y):
		zV = Vector2(1.0, 0.0)
		wV = 0.0
	else:
		zV = Vector2(segment.via.coords.x, segment.via.coords.y)
		wV = 1.0

	var sv := _hdet(zS, wS, zV, wV)
	var ep := _hdet(zE, wE, zP, wP)
	var sp := _hdet(zS, wS, zP, wP)
	var ev := _hdet(zE, wE, zV, wV)

	var num_re: float = sv.x * ep.x - sv.y * ep.y
	var num_im: float = sv.x * ep.y + sv.y * ep.x
	var den_re: float = sp.x * ev.x - sp.y * ev.y
	var den_im: float = sp.x * ev.y + sp.y * ev.x
	return (num_re * den_re + num_im * den_im) >= 0.0

static func _intersect_ray_carrier(ray: Ray, carrier: GeneralizedCircle) -> Array:
	var dir := ray.direction.to_vector()
	if dir.length_squared() == 0.0:
		return []

	var ox := ray.origin.coords.x
	var oy := ray.origin.coords.y
	var dx := dir.x
	var dy := dir.y

	var qa := carrier.a * (dx * dx + dy * dy)
	var qb := 2.0 * carrier.a * (ox * dx + oy * dy) + carrier.b * dx + carrier.c * dy
	var qc := carrier.a * (ox * ox + oy * oy) + carrier.b * ox + carrier.c * oy + carrier.d

	var t_values: Array[float] = []
	if qa == 0.0:
		if qb == 0.0:
			return []
		t_values.append(-qc / qb)
	else:
		var discriminant := qb * qb - 4.0 * qa * qc
		if discriminant < 0.0:
			return []
		elif discriminant == 0.0:
			t_values.append(-qb / (2.0 * qa))
		else:
			var sqrt_d := sqrt(discriminant)
			t_values.append((-qb - sqrt_d) / (2.0 * qa))
			t_values.append((-qb + sqrt_d) / (2.0 * qa))

	var results: Array = []
	for t in t_values:
		var point := ray.origin.coords + t * dir
		results.append({"t": t, "point": point})
	return results

static func ray_to_line(ray: Ray) -> GeneralizedCircle:
	var dir := ray.direction.to_vector()
	var ox := ray.origin.coords.x
	var oy := ray.origin.coords.y
	return GeneralizedCircle.from_line(dir.y, -dir.x, dir.x * oy - dir.y * ox)

static func inversive_pullback_intersect(ray: Ray, visual_carrier: GeneralizedCircle, frame: MobiusTransform) -> Array:
	var ray_line := ray_to_line(ray)
	var visual_ray := _hermitian_transform_f64(ray_line, frame)
	var visual_hits := intersect_circles(visual_ray, visual_carrier)
	if visual_hits.is_empty():
		return []

	var frame_inv := frame.invert()
	var dir := ray.direction.to_vector()
	var results: Array = []
	for vh in visual_hits:
		var norm_point := frame_inv.apply(vh)
		var t := project_point_on_ray(ray, norm_point)
		var on_ray_point := ray.origin.coords + t * dir
		results.append({"t": t, "point": on_ray_point})
	return results

# Float64 Hermitian congruence H' = N† H N, avoiding Vector2 float32 truncation.
static func _hermitian_transform_f64(circle: GeneralizedCircle, mobius: MobiusTransform) -> GeneralizedCircle:
	var h_w_re: float = circle.b / 2.0
	var h_w_im: float = -circle.c / 2.0

	var ax: float = mobius.a_re; var ay: float = mobius.a_im
	var bx: float = mobius.b_re; var by: float = mobius.b_im
	var cx: float = mobius.c_re; var cy: float = mobius.c_im
	var dx: float = mobius.d_re; var dy: float = mobius.d_im

	var n00_x: float; var n00_y: float
	var n01_x: float; var n01_y: float
	var n10_x: float; var n10_y: float
	var n11_x: float; var n11_y: float
	var h01_x: float; var h01_y: float
	var h10_x: float; var h10_y: float

	if mobius.conjugating:
		n00_x = dx; n00_y = -dy
		n01_x = -bx; n01_y = by
		n10_x = -cx; n10_y = cy
		n11_x = ax; n11_y = -ay
		h01_x = h_w_re; h01_y = -h_w_im
		h10_x = h_w_re; h10_y = h_w_im
	else:
		n00_x = dx; n00_y = dy
		n01_x = -bx; n01_y = -by
		n10_x = -cx; n10_y = -cy
		n11_x = ax; n11_y = ay
		h01_x = h_w_re; h01_y = h_w_im
		h10_x = h_w_re; h10_y = -h_w_im

	var nh00_x := n00_x; var nh00_y := -n00_y
	var nh01_x := n10_x; var nh01_y := -n10_y
	var nh10_x := n01_x; var nh10_y := -n01_y
	var nh11_x := n11_x; var nh11_y := -n11_y

	var h_a: float = circle.a
	var h_d: float = circle.d

	var t00_x := nh00_x * h_a + (nh01_x * h10_x - nh01_y * h10_y)
	var t00_y := nh00_y * h_a + (nh01_x * h10_y + nh01_y * h10_x)
	var t01_x := (nh00_x * h01_x - nh00_y * h01_y) + nh01_x * h_d
	var t01_y := (nh00_x * h01_y + nh00_y * h01_x) + nh01_y * h_d
	var t10_x := nh10_x * h_a + (nh11_x * h10_x - nh11_y * h10_y)
	var t10_y := nh10_y * h_a + (nh11_x * h10_y + nh11_y * h10_x)
	var t11_x := (nh10_x * h01_x - nh10_y * h01_y) + nh11_x * h_d
	var t11_y := (nh10_x * h01_y + nh10_y * h01_x) + nh11_y * h_d

	var r00 := (t00_x * n00_x - t00_y * n00_y) + (t01_x * n10_x - t01_y * n10_y)
	var r01_x := (t00_x * n01_x - t00_y * n01_y) + (t01_x * n11_x - t01_y * n11_y)
	var r01_y := (t00_x * n01_y + t00_y * n01_x) + (t01_x * n11_y + t01_y * n11_x)
	var r11 := (t10_x * n01_x - t10_y * n01_y) + (t11_x * n11_x - t11_y * n11_y)

	var new_b := 2.0 * r01_x
	var new_c := -2.0 * r01_y
	if r00 != 0.0 and absf(r00) < 1e-10 * maxf(absf(new_b), maxf(absf(new_c), absf(r11))):
		r00 = 0.0
	return GeneralizedCircle.new(r00, new_b, new_c, r11)

static func project_point_on_ray(ray: Ray, point: Vector2) -> float:
	var dir := ray.direction.to_vector()
	var dir_len_sq := dir.length_squared()
	if dir_len_sq == 0.0:
		return 0.0
	return (point - ray.origin.coords).dot(dir) / dir_len_sq

static func project_onto_carrier(point: Vector2, carrier: GeneralizedCircle) -> Vector2:
	if carrier.is_line():
		var b := carrier.b
		var c := carrier.c
		var d := carrier.d
		var denom := b * b + c * c
		var f := b * point.x + c * point.y + d
		return Vector2(point.x - f * b / denom, point.y - f * c / denom)
	else:
		var center := carrier.center()
		var r := carrier.radius()
		var diff := point - center
		var dist := diff.length()
		if dist < 1e-12:
			return center + Vector2(r, 0)
		return center + r * diff / dist

static func _determine_side(ray: Ray, point: Vector2, seg: Segment) -> Side.Value:
	var carrier: GeneralizedCircle = seg.get_carrier()
	var dir := ray.direction.to_vector().normalized()
	var grad := Vector2(2.0 * carrier.a * point.x + carrier.b,
						2.0 * carrier.a * point.y + carrier.c)
	var approach_f_sign := -dir.dot(grad)
	var winding := seg._compute_winding()
	if winding >= 0.0:
		return Side.Value.LEFT if approach_f_sign > 0.0 else Side.Value.RIGHT
	else:
		return Side.Value.RIGHT if approach_f_sign > 0.0 else Side.Value.LEFT

static func _hdet(zA: Vector2, wA: float, zB: Vector2, wB: float) -> Vector2:
	return Vector2(zA.x * wB - zB.x * wA, zA.y * wB - zB.y * wA)

static func at_which_endpoint(point: Vector2, segment: Segment) -> int:
	if segment.full:
		return 0
	if point == segment.start.coords:
		return 1
	if point == segment.end.coords:
		return 2
	return 0

static func tangent_into_segment(segment: Segment, which_endpoint: int) -> Vector2:
	var carrier := segment.get_carrier()
	if carrier.is_line():
		if which_endpoint == 1:
			return (segment.end.coords - segment.start.coords).normalized()
		else:
			return (segment.start.coords - segment.end.coords).normalized()
	var center := carrier.center()
	var ep := segment.start.coords if which_endpoint == 1 else segment.end.coords
	var radius_dir := ep - center
	var perp_ccw := Vector2(-radius_dir.y, radius_dir.x)
	var perp_cw := Vector2(radius_dir.y, -radius_dir.x)
	var ref := segment.via.coords - ep
	if perp_ccw.dot(ref) >= perp_cw.dot(ref):
		return perp_ccw.normalized()
	return perp_cw.normalized()

static func endpoint_blocked_sides(point: Vector2, segment: Segment, ray: Ray, which_endpoint: int) -> Array:
	if which_endpoint == 0:
		var on_seg := is_on_segment(point, segment)
		return [on_seg, on_seg]
	var tangent := tangent_into_segment(segment, which_endpoint)
	var ray_dir := ray.direction.to_vector().normalized()
	var cross := ray_dir.cross(tangent)
	if cross == 0.0:
		return [true, true]
	if cross > 0:
		return [false, true]
	return [true, false]

static func intersect_circles(c1: GeneralizedCircle, c2: GeneralizedCircle) -> Array[Vector2]:
	var is_line1 := c1.is_line()
	var is_line2 := c2.is_line()

	if is_line1 and is_line2:
		return _intersect_line_line(c1, c2)
	if is_line1:
		return _intersect_line_circle(c1, c2)
	if is_line2:
		return _intersect_line_circle(c2, c1)

	var cx1 := -c1.b / (2.0 * c1.a)
	var cy1 := -c1.c / (2.0 * c1.a)
	var r1 := c1.radius()
	var cx2 := -c2.b / (2.0 * c2.a)
	var cy2 := -c2.c / (2.0 * c2.a)
	var r2 := c2.radius()

	var dx := cx2 - cx1
	var dy := cy2 - cy1
	var d_sq := dx * dx + dy * dy
	var d := sqrt(d_sq)

	if d < 1e-12:
		return []
	if d > r1 + r2 + 1e-10:
		return []
	if d < absf(r1 - r2) - 1e-10:
		return []

	var a_param := (r1 * r1 - r2 * r2 + d_sq) / (2.0 * d)
	var h_sq := r1 * r1 - a_param * a_param
	if h_sq < 0.0:
		h_sq = 0.0
	var h := sqrt(h_sq)

	var mx := cx1 + a_param * dx / d
	var my := cy1 + a_param * dy / d

	if h < 1e-12:
		return [Vector2(mx, my)] as Array[Vector2]

	var px := -dy / d * h
	var py := dx / d * h
	return [Vector2(mx + px, my + py), Vector2(mx - px, my - py)] as Array[Vector2]

static func _intersect_line_line(l1: GeneralizedCircle, l2: GeneralizedCircle) -> Array[Vector2]:
	var det := l1.b * l2.c - l1.c * l2.b
	if absf(det) < 1e-12:
		return []
	var x := (l1.c * l2.d - l2.c * l1.d) / det
	var y := (l2.b * l1.d - l1.b * l2.d) / det
	var result: Array[Vector2] = [Vector2(x, y)]
	return result

static func _intersect_line_circle(line: GeneralizedCircle, circ: GeneralizedCircle) -> Array[Vector2]:
	var cx := -circ.b / (2.0 * circ.a)
	var cy := -circ.c / (2.0 * circ.a)
	var r := circ.radius()

	var lb := line.b
	var lc := line.c
	var ld := line.d
	var len_sq := lb * lb + lc * lc
	if len_sq < 1e-24:
		return []
	var dist := (lb * cx + lc * cy + ld) / sqrt(len_sq)
	if absf(dist) > r + 1e-10:
		return []

	var foot_x := cx - lb * dist / sqrt(len_sq)
	var foot_y := cy - lc * dist / sqrt(len_sq)

	var h_sq := r * r - dist * dist
	if h_sq < 0.0:
		h_sq = 0.0
	var h := sqrt(h_sq)

	if h < 1e-12:
		return [Vector2(foot_x, foot_y)] as Array[Vector2]

	var dir_x := -lc / sqrt(len_sq)
	var dir_y := lb / sqrt(len_sq)
	return [
		Vector2(foot_x + dir_x * h, foot_y + dir_y * h),
		Vector2(foot_x - dir_x * h, foot_y - dir_y * h)] as Array[Vector2]
