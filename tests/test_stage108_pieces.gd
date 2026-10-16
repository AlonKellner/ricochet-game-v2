extends GutTest

# Stage 108: pieces (GAME_SPEC 9.7) -- the counts of the spec's examples,
# shared ends, the carrier kept, crossing-free surfaces untouched.

func _side() -> SideConfig:
	return SideConfig.new(TerminalEffect.new(), false)

func _line(a: Vector2, b: Vector2) -> Surface:
	return Surface.new(Segment.from_coords(a, b, (a + b) / 2.0), _side(), _side())

func _circle(c: Vector2, r: float) -> Surface:
	var g := GeneralizedCircle.from_circle(c, r)
	return Surface.new(Segment.full_from_carrier(g), _side(), _side())

func _count(p: Pieces, surfaces: Array) -> int:
	var n := 0
	for s in surfaces:
		n += p.of(s).size()
	return n

func before_each():
	Surface.reset_id_counter()

func test_stage108_the_spec_counts():
	var two := [_line(Vector2(0, 50), Vector2(100, 50)), _line(Vector2(50, 0), Vector2(50, 100))]
	assert_eq(_count(Pieces.build(two), two), 4, "two crossing lines: 4 pieces")
	var seven := [_line(Vector2(0, 50), Vector2(100, 50)), _line(Vector2(30, 0), Vector2(30, 100)), _line(Vector2(70, 0), Vector2(70, 100))]
	assert_eq(_count(Pieces.build(seven), seven), 7, "one line crossing two others: 7")
	# a triangle of lines extended past each other's corners
	var nine := [_line(Vector2(-10, 0), Vector2(110, 0)), _line(Vector2(-5, -10), Vector2(55, 110)),
		_line(Vector2(105, -10), Vector2(45, 110))]
	assert_eq(_count(Pieces.build(nine), nine), 9, "three inter-crossing lines: 9")

func test_stage108_pieces_share_ends_and_keep_the_carrier():
	var a := _line(Vector2(500, -300), Vector2(500, 300))
	var b := _line(Vector2(400, 200), Vector2(600, 200))
	var p := Pieces.build([a, b])
	var pa: Array = p.of(a)
	var pb: Array = p.of(b)
	assert_eq(pa.size(), 2)
	assert_eq(pa[0].segment.start.coords, Vector2(500, -300), "the first piece starts at the start")
	assert_eq(pa[0].segment.end.coords, pa[1].segment.start.coords, "consecutive pieces share their ends")
	assert_eq(pa[1].segment.end.coords, Vector2(500, 300), "the last ends at the end")
	assert_eq(pa[0].segment.end.coords, pb[0].segment.end.coords, "both surfaces' pieces share the crossing")
	assert_eq(pa[0].segment.get_carrier(), a.segment.get_carrier(), "the design surface's carrier, the same object")
	assert_true(pa[0].contains(Vector2(500, 0)))
	assert_false(pa[0].contains(Vector2(500, 250)))
	assert_eq(p.crossings, [Vector2(500, 200)] as Array[Vector2])

func test_stage108_crossing_free_surfaces_are_their_own_piece():
	var a := _line(Vector2(0, 0), Vector2(100, 0))
	var b := _line(Vector2(0, 10), Vector2(100, 10))
	var p := Pieces.build([a, b])
	assert_eq(p.of(a).size(), 1)
	assert_eq(p.of(a)[0].index, -1, "the whole surface")
	assert_eq(p.of(a)[0].segment, a.segment, "its own segment object")
	assert_null(p.of(a)[0].span, "no span: a plan entry on it matches the whole surface")

func test_stage108_circle_pieces():
	var c := _circle(Vector2(0, 0), 5.0)
	var l := _line(Vector2(3, -10), Vector2(3, 10))
	var p := Pieces.build([c, l])
	assert_eq(p.of(c).size(), 2, "a full circle crossed twice: two arcs")
	assert_eq(p.of(l).size(), 3)
	for piece in p.of(c):
		assert_false(piece.segment.full)
		assert_eq(piece.segment.get_carrier(), c.segment.get_carrier())
		assert_almost_eq(piece.segment.via.coords.length(), 5.0, 1e-4, "the via lies on the circle")
	var one := _line(Vector2(3, -10), Vector2(3, 0))   # ends inside: one crossing
	Surface.reset_id_counter()
	var c2 := _circle(Vector2(0, 0), 5.0)
	var p2 := Pieces.build([c2, one])
	assert_eq(p2.of(c2).size(), 1, "one cut leaves a full circle whole")

func test_stage108_portals_and_pre_transforms_are_not_split():
	var src := Segment.from_coords(Vector2(0, -50), Vector2(0, 50), Vector2(0, 0))
	var pair := RigidMotionEffect.create_portal_pair(src, 0.0, Vector2(300, 0))
	var portal := Surface.new(src, SideConfig.new(pair.source_effect, true), SideConfig.new(pair.source_effect, true))
	var crossing := _line(Vector2(-20, 0), Vector2(20, 0))
	var p := Pieces.build([portal, crossing])
	assert_eq(p.of(portal).size(), 1)
	assert_eq(p.of(crossing).size(), 1)
	assert_eq(p.portal_crossings.size(), 1, "reported for level validation")
