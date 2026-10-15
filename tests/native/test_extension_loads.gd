extends GutTest
## Stage 89: the GDExtension toolchain works end to end.
##
## Requires `make native` (builds native/bin/ and runs the headless import that
## registers ricochet.gdextension). Each test exercises one binding mechanism
## later stages depend on; see native/gd/gd_native_probe.hpp.


func test_engine_is_at_least_4_7() -> void:
	var v := Engine.get_version_info()
	assert_true(v.major > 4 or (v.major == 4 and v.minor >= 7),
		"Godot %s is older than 4.7" % v.string)


func test_extension_class_is_registered() -> void:
	assert_true(ClassDB.class_exists("NativeProbe"), "NativeProbe missing: was the extension loaded?")
	assert_true(ClassDB.class_exists("NativeProbeChild"))
	assert_true(ClassDB.can_instantiate("NativeProbe"))


func test_probe_is_a_refcounted_not_a_node() -> void:
	assert_true(ClassDB.is_parent_class("NativeProbe", "RefCounted"))
	assert_false(ClassDB.is_parent_class("NativeProbe", "Node"))
	var p := NativeProbe.new()
	assert_true(p is RefCounted)
	# `p is Node` is a static type error here, which already proves the parser
	# knows NativeProbe; the runtime check needs is_instance_of.
	assert_false(is_instance_of(p, Node))


func test_static_method() -> void:
	assert_eq(NativeProbe.add(2, 3), 5)
	assert_eq(NativeProbe.add(-7, 7), 0)


func test_version_string_reaches_gdscript() -> void:
	var v: String = NativeProbe.version()
	assert_ne(v, "", "rc::RC_VERSION is empty")
	assert_true(v.begins_with("0.89"), "unexpected native version %s" % v)


func test_enum_is_visible_both_ways() -> void:
	assert_eq(NativeProbe.Mode.PLANNED, 1)
	assert_eq(NativeProbe.Mode.PHYSICAL, 0)
	assert_eq(NativeProbe.PLANNED, 1)
	assert_eq(NativeProbe.PHYSICAL, 0)
	assert_true(ClassDB.class_has_enum("NativeProbe", "Mode"))
	assert_eq(ClassDB.class_get_integer_constant_enum("NativeProbe", "PLANNED"), &"Mode")


func test_integer_constant() -> void:
	assert_eq(NativeProbe.MAX_HITS, 32)
	assert_true(ClassDB.class_has_integer_constant("NativeProbe", "MAX_HITS"))


func test_property_in_operator_and_get_set() -> void:
	var p := NativeProbe.new()
	assert_true("kind" in p, "'kind' in obj must see the ADD_PROPERTY property")
	assert_false("no_such_property" in p)
	assert_eq(p.kind, "")
	p.kind = "reflector"
	assert_eq(p.kind, "reflector")
	assert_eq(p.get_kind(), "reflector")
	p.set("kind", "portal")
	assert_eq(p.get("kind"), "portal")


func test_method_returns_another_registered_refcounted() -> void:
	var p := NativeProbe.new()
	var child = p.make_child(2.5)
	assert_not_null(child)
	assert_true(child is NativeProbeChild)
	assert_eq(child.get_class(), "NativeProbeChild")
	assert_eq(child.value, 2.5)
	child.value = -1.25
	assert_eq(child.value, -1.25)
	assert_true("value" in child)


func test_packed_float64_round_trip_is_bit_exact() -> void:
	var p := NativeProbe.new()
	var neg_zero := -0.0
	# GDScript quirk: a function's constant pool deduplicates 0.0 and -0.0 (they
	# compare equal), so after the -0.0 above every 0.0 literal here would be
	# negative zero too. Build +0.0 at runtime instead.
	var pos_zero := absf(neg_zero)
	assert_eq(1.0 / pos_zero, INF, "+0.0 must be positive zero")
	assert_eq(1.0 / neg_zero, -INF, "-0.0 must be negative zero")
	var input := PackedFloat64Array([pos_zero, neg_zero, 1.0, -1.0, 1e300, -1e300, 2.5e-308, 0.1, 1.0 / 3.0])
	var expected := PackedFloat64Array()
	for x in input:
		expected.append(2.0 * x)
	var got: PackedFloat64Array = p.doubled(input)
	assert_eq(got.size(), input.size())
	assert_eq(got.to_byte_array(), expected.to_byte_array(), "doubled values must be bit-identical")
	# the sign of zero survives the round trip (byte 7 of a little-endian double holds the sign bit)
	assert_eq(got.to_byte_array()[1 * 8 + 7] & 0x80, 0x80, "-0.0 lost its sign")
	assert_eq(got.to_byte_array()[0 * 8 + 7] & 0x80, 0, "+0.0 gained a sign")
	assert_eq(got[4], 2e300)
	# an empty array is a legal round trip too
	assert_eq(p.doubled(PackedFloat64Array()).size(), 0)


func test_dictionary_key_count() -> void:
	var p := NativeProbe.new()
	assert_eq(p.key_count({}), 0)
	assert_eq(p.key_count({"a": 1, "b": 2, 3: "c"}), 3)
	var d := {}
	for i in range(100):
		d[i] = i * i
	assert_eq(p.key_count(d), 100)


func test_error_sink_routes_to_push_error() -> void:
	var p := NativeProbe.new()
	assert_true(p.sink_error_test(), "sink_error_test must return true after reporting")
	# The rc error sink is wired to UtilityFunctions::push_error in
	# initialize_ricochet_module; GUT tracks that push_error and this assert
	# both proves it arrived and marks it handled so the test does not fail on it.
	assert_push_error("intentional test error routed through the rc error sink")
