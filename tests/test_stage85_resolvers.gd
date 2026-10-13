extends GutTest

# Stage 85: ConfigResolver (GAME_SPEC 9.4) -- a surface's active side config
# under the game state's flags.

func _seg() -> Segment:
	return Segment.from_coords(Vector2(0, 0), Vector2(0, 100), Vector2(0, 50))

func test_fixed_resolver_default():
	var l := SideConfig.new(ReflectionEffect.new(_seg().get_carrier()), true)
	var r := SideConfig.new(null, false)
	var res := FixedResolver.new(l, r)
	for flags in [{}, {"open": true}, {"k": 2}]:
		assert_eq(res.resolve(Side.Value.LEFT, GameState.new(flags)), l)
		assert_eq(res.resolve(Side.Value.RIGHT, GameState.new(flags)), r)
	# a plain surface is fixed
	var s := Surface.new(_seg(), l, r)
	assert_true(s.resolver is FixedResolver)

func test_categorical_resolver_lookup_and_DEFAULT_fallback():
	var mirror := SideConfig.new(ReflectionEffect.new(_seg().get_carrier()), true)
	var block := SideConfig.new(TerminalEffect.new(), false)
	var open := SideConfig.new(null, false)
	var res := CategoricalResolver.new("door", {
		"true": [mirror, mirror],
		"false": [open, open],
		"DEFAULT": [block, null],
	})
	assert_eq(res.resolve(Side.Value.LEFT, GameState.new({"door": true})), mirror)
	assert_eq(res.resolve(Side.Value.RIGHT, GameState.new({"door": false})), open)
	# missing key -> DEFAULT
	assert_eq(res.resolve(Side.Value.LEFT, GameState.new({})), block)
	assert_null(res.resolve(Side.Value.RIGHT, GameState.new({})))
	# a value without an entry -> DEFAULT
	assert_eq(res.resolve(Side.Value.LEFT, GameState.new({"door": 7})), block)
	# no DEFAULT: the first entry (with a warning)
	var res2 := CategoricalResolver.new("k", {"1": [mirror, mirror], "2": [block, block]})
	assert_eq(res2.resolve(Side.Value.LEFT, GameState.new({})), mirror)
	# values are keyed by their JSON spelling: 2 and 2.0 are the same key
	assert_eq(res2.resolve(Side.Value.LEFT, GameState.new({"k": 2.0})), block)
	assert_eq(res2.resolve(Side.Value.LEFT, GameState.new({"k": 2})), block)

func test_active_side_config_delegates():
	var mirror := SideConfig.new(ReflectionEffect.new(_seg().get_carrier()), true)
	var open := SideConfig.new(null, false)
	var res := CategoricalResolver.new("door", {"true": [mirror, mirror], "DEFAULT": [open, open]})
	var s := Surface.with_resolver(_seg(), res)
	assert_eq(s.active_side_config(Side.Value.LEFT, GameState.new({"door": true})), mirror)
	assert_eq(s.active_side_config(Side.Value.LEFT, GameState.new({})), open)
	# scene defs describe the table (oracle format)
	var d := BeamScene.describe_surface(s)
	assert_eq(d["resolver"]["key"], "door")
	assert_eq(d["resolver"]["table"]["true"]["left"]["type"], "reflection")
	assert_eq(d["resolver"]["table"]["DEFAULT"]["left"]["type"], "pass")
	assert_null(d["left"])
