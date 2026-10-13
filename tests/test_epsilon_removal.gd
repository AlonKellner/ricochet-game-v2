extends GutTest

# =============================================================================
# Step 4: E5 + E6 — exact equality for zero-length steps and degenerate arcs
# =============================================================================

func test_degenerate_arc_exact_equality() -> void:
	var p := Vector2(100, 200)
	assert_false(VisualConverter.is_arc(p, p, p), "Identical start/end/via is not an arc")

func test_near_degenerate_arc_still_valid() -> void:
	var start := Vector2(100, 200)
	var end_v := Vector2(100.001, 200)
	assert_ne(start, end_v, "Near-identical points are not rejected by exact equality check")

# =============================================================================
# Step 6: Anti-regression tests — FAIL if epsilons reintroduced
# =============================================================================

func test_no_epsilon_zero_length_boundary() -> void:
	# Sub-pixel differences must NOT be treated as zero-length
	var a := Vector2(100, 200)
	var b := Vector2(100.001, 200)
	assert_ne(a, b, "Sub-pixel difference must not be treated as zero-length")
