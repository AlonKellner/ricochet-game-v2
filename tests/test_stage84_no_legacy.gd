extends GutTest

# Stage 84: the legacy frame-based solver is gone. The beam solver is the
# only solver; effects carry no Möbius / tracked-transform state; portal pairs
# and self-links are built from definitions, not from frame provenance.

const DELETED_FILES := [
	"res://scripts/math/tracer.gd",
	"res://scripts/math/planner.gd",
	"res://scripts/math/transform_cache.gd",
	"res://scripts/math/tracked_transform.gd",
	"res://scripts/math/mobius_transform.gd",
	"res://scripts/math/intersection.gd",
	"res://scripts/testing/solver_parity.gd",
]

# whole-identifier patterns that must not appear in code (comments stripped)
const FORBIDDEN := {
	"Tracer.": "(?<![A-Za-z_])Tracer\\.",
	"Planner.": "(?<![A-Za-z_])Planner\\.",
	"TransformCache": "TransformCache",
	"TrackedTransform": "TrackedTransform",
	"MobiusTransform": "MobiusTransform",
	"Intersection.": "(?<![A-Za-z_])Intersection\\.",
	"transformed_by": "transformed_by",
	"inversive_pullback": "inversive_pullback",
	"Effect.normalized(carrier)": "\\.normalized\\([^)]",
	"get_mobius": "get_(inverse_)?mobius",
	"get_tracked_transform": "get_tracked_transform",
	"_build_normalized": "_build_normalized",
	"apply_forward": "apply_forward",
	"LEGACY_FALLBACK": "LEGACY_FALLBACK",
	"use_beam_tracer": "use_beam_tracer",
	"s0_tracer": "s0_tracer",
	"Point.transformed": "func transformed\\(",
}

static func _gd_files(dir_path: String, out: Array) -> void:
	var d := DirAccess.open(dir_path)
	if d == null:
		return
	for f in d.get_files():
		if f.ends_with(".gd"):
			out.append(dir_path.path_join(f))
	for sub in d.get_directories():
		_gd_files(dir_path.path_join(sub), out)

# source without comments (a '#' outside a string starts a comment)
static func _code_of(src: String) -> String:
	var out := PackedStringArray()
	for line in src.split("\n"):
		var q := ""
		var cut := line.length()
		for i in line.length():
			var ch := line[i]
			if q != "":
				if ch == q:
					q = ""
			elif ch == "\"" or ch == "'":
				q = ch
			elif ch == "#":
				cut = i
				break
		out.append(line.substr(0, cut))
	return "\n".join(out)

func test_no_legacy_symbols():
	for f in DELETED_FILES:
		assert_false(FileAccess.file_exists(f), "%s is deleted" % f)
	var files: Array = []
	_gd_files("res://scripts", files)
	assert_gt(files.size(), 30)
	var found: Array = []
	for path in files:
		var code := _code_of(FileAccess.open(path, FileAccess.READ).get_as_text())
		for name in FORBIDDEN:
			var m := RegEx.create_from_string(FORBIDDEN[name]).search(code)
			if m != null:
				found.append("%s: %s" % [path, name])
	assert_eq(found, [], "no legacy solver symbols in scripts/")

func test_surface_self_links_without_tracked():
	Surface.reset_id_counter()
	var seg := Segment.from_coords(Vector2(0, 0), Vector2(0, 100), Vector2(0, 50))
	var arc := Segment.from_coords(Vector2(100, 0), Vector2(100, 200), Vector2(200, 100))
	var cases := [
		[ReflectionEffect.new(seg.get_carrier()), seg, true],
		[ReflectionEffect.new(arc.get_carrier()), arc, true],
		[CircleInversionEffect.new(arc.get_carrier()), arc, true],
		[LineNormalProjection.new(), seg, false],
		[TerminalEffect.new(), seg, false],
	]
	for c in cases:
		var eff: Effect = c[0]
		assert_eq(eff.is_self_inverse(), c[2], "%s self-inverse" % eff.get_display_name())
		var cfg := SideConfig.new(eff, true)
		var s := Surface.new(c[1], cfg, cfg)
		for side in [Side.Value.LEFT, Side.Value.RIGHT]:
			var link: SideLink = s.get_side_link(side)
			if c[2]:
				assert_not_null(link, "%s auto-links to itself" % eff.get_display_name())
				if link != null:
					assert_eq(link.surface, s)
					assert_eq(link.side, side)
			else:
				assert_null(link, "%s is not self-linked" % eff.get_display_name())
	# a non-interactive self-inverse side is not linked
	var inert := Surface.new(seg, SideConfig.new(ReflectionEffect.new(seg.get_carrier()), false), null)
	assert_null(inert.get_side_link(Side.Value.LEFT))
	# a portal is not self-inverse: never self-linked
	var pp := RigidMotionEffect.create_portal_pair(seg, 0.0, Vector2(300, 0))
	assert_false(pp.source_effect.is_self_inverse())
	var src := FileAccess.open("res://scripts/game/surface.gd", FileAccess.READ).get_as_text()
	assert_false(src.contains("tracked"), "Surface links by is_self_inverse, not tracked transforms")

# T(z) = e^{iθ} z + d, evaluated as complex multiply-add (closed form)
static func _t(z: Vector2, theta: float, d: Vector2) -> Vector2:
	var c := cos(theta)
	var s := sin(theta)
	return Vector2(c * z.x - s * z.y + d.x, s * z.x + c * z.y + d.y)

func test_portal_pair_construction_without_frames():
	var segs := [
		Segment.from_coords(Vector2(100, 100), Vector2(100, 300), Vector2(100, 200)),
		Segment.from_coords(Vector2(200, 0), Vector2(200, 200), Vector2(300, 100)),
	]
	for seg: Segment in segs:
		for theta in [0.0, PI / 2.0, 0.7, -2.1]:
			var d := Vector2(400, -50)
			var pp := RigidMotionEffect.create_portal_pair(seg, theta, d)
			var tseg: Segment = pp.target_segment
			assert_eq(tseg.start.coords, _t(seg.start.coords, theta, d), "start θ=%s" % theta)
			assert_eq(tseg.end.coords, _t(seg.end.coords, theta, d), "end θ=%s" % theta)
			assert_eq(tseg.via.coords, _t(seg.via.coords, theta, d), "via θ=%s" % theta)
			var a: RigidMotionEffect = pp.source_effect
			var b: RigidMotionEffect = pp.target_effect
			assert_eq(a.partner(), b, "source pairs with target")
			assert_eq(b.partner(), a, "target pairs with source")
			# the inverse motion: θ' = -θ, d' = -e^{-iθ} d
			assert_eq(b._theta, -theta)
			assert_eq(b._d, -_t(d, -theta, Vector2.ZERO))
			# carriers of the effects are the segments' carriers
			assert_eq(a._carrier, seg.get_carrier())
			assert_eq(b._carrier, tseg.get_carrier())
	# beam scene pairing reads the partner (no tracked transforms)
	Surface.reset_id_counter()
	var seg0: Segment = segs[0]
	var pp0 := RigidMotionEffect.create_portal_pair(seg0, 0.0, Vector2(500, 0))
	var sa := Surface.new(seg0, SideConfig.new(pp0.source_effect, true), SideConfig.new(pp0.source_effect, true))
	var sb := Surface.new(pp0.target_segment, SideConfig.new(pp0.target_effect, true), SideConfig.new(pp0.target_effect, true))
	var defs := BeamScene.defs_from_surfaces([sa, sb])
	assert_eq(int(defs[0]["left"]["pair"]), sb.id)
	assert_eq(int(defs[1]["left"]["pair"]), sa.id)
	# no frame provenance left on points
	assert_false("transforms" in Point.at(Vector2.ZERO), "Point carries no transform provenance")
