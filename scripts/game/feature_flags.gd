class_name FeatureFlags
extends RefCounted

# Feature switches of the game layer.
#
# INFINITY_EVENT (Stage 88, B9): the beam solver emits the `infinity` event
# where a straight beam wraps through the level bounds and the ∞ plan entry
# (an edge click) can be consumed; it is counted toward the hit limit, so it
# moves the MAX_HITS cut of long traces (docs/research/stage88b_fixture_diff.md).
# Off reproduces the pre-Stage-88 traces exactly; the committed fixtures and
# goldens are those. The user approves the flip (the fixtures regenerate with
# it). Tests force the switch with `infinity_event_override`.
const INFINITY_EVENT_DEFAULT := false

static var infinity_event_override = null   # null: the default; a bool: forced

static func infinity_event() -> bool:
	if infinity_event_override != null:
		return infinity_event_override
	return INFINITY_EVENT_DEFAULT
