extends GutTest

# Stage 88: plannable infinity. The oracle (docs/research/oracle/trace2.py,
# EMIT_INFINITY) specifies the "infinity" event and the INFINITY_ID plan entry;
# bench/fixtures/stage88_infinity.json holds its traces. The solver does not
# emit the event before Stage 88c: the trace comparison is pending until then.
# This file already pins the fixture schema so the spec cannot drift unnoticed.

const FIXTURES := "res://bench/fixtures/stage88_infinity.json"
const INFINITY_ID := -3

var _fx: Dictionary = {}

func before_all():
	var f := FileAccess.open(FIXTURES, FileAccess.READ)
	assert_not_null(f, "fixture file exists")
	var d: Dictionary = JSON.parse_string(f.get_as_text())
	for x in d["fixtures"]:
		_fx[x["name"]] = x

func test_fixture_schema():
	assert_eq(_fx.size(), 26, "26 Stage 88 fixtures")
	var infinity_events := 0
	var consumed := 0
	for name in _fx:
		var x: Dictionary = _fx[name]
		for e in x["expected"]:
			assert_true(e["kind"] in ["hit", "cursor", "origin", "escape", "max", "infinity"], "%s: kind %s" % [name, e["kind"]])
			if e["kind"] == "infinity":
				infinity_events += 1
				assert_eq(int(e["surface_id"]), INFINITY_ID, "%s: infinity carries INFINITY_ID" % name)
				assert_false(e.has("x"), "%s: infinity has no point" % name)
				if e["applied"]:
					consumed += 1
		for p in x["plan"]:
			if int(p["surface_id"]) == INFINITY_ID:
				assert_eq(p["side"], "", "%s: the infinity entry has no side" % name)
	assert_gt(infinity_events, 0)
	assert_gt(consumed, 0, "some fixtures consume the infinity entry")

func test_both_realizations_of_one_mirror_plan_are_recorded():
	var direct: Array = _fx["mirror_direct_reaches_through_unapplied_wrap_planned"]["expected"]
	var other: Array = _fx["infinity_then_mirror_planned"]["expected"]
	assert_eq([direct[0]["kind"], direct[1]["kind"], direct[2]["kind"]], ["hit", "infinity", "cursor"])
	assert_false(direct[1]["applied"])
	assert_eq([other[0]["kind"], other[1]["kind"], other[2]["kind"]], ["infinity", "hit", "cursor"])
	assert_true(other[0]["applied"])

func test_solver_matches_stage88_fixtures():
	pending("Stage 88c: the native solver emits the infinity event and consumes the infinity plan entry")
