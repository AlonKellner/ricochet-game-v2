class_name BeamGeom
extends RefCounted

# Stage 80 RED stub: precision-generic segment geometry for the beam solver.
# API only; every function returns a deliberately wrong result until GREEN.

enum Kind { LINE, CIRCLE }

class Seg extends RefCounted:
	var id: int
	var kind: int = Kind.LINE
	var full: bool = false
	var s: CN
	var e: CN
	var v = null       # CN, or null = the point at infinity (full line)
	var c: CN = null   # circle center
	var r2 = null      # circle radius squared

# Build from fixture/level data: {kind: line|arc|full_circle, start, end, via, center, radius}
static func seg_from_dict(_ctx: NumCtx, _d: Dictionary) -> Seg:
	return Seg.new()

static func seg_from_segment(_ctx: NumCtx, _segment: Segment, _sid: int) -> Seg:
	return Seg.new()

# Real roots t of beam ∩ seg's carrier. depart_on: beam.p lies exactly on it.
static func hits(_beam: HPBeam, _seg: Seg, _depart_on: bool) -> Array:
	return []

# Re(cross ratio) of the on-segment test; on-segment iff >= 0.
static func on_segment_value(_seg: Seg, _p: CN):
	return null

static func on_segment(_seg: Seg, _p: CN) -> bool:
	return false

# 1 / 2 if p is exactly the start / end endpoint, else 0.
static func endpoint_at(_seg: Seg, _p: CN) -> int:
	return 0

# Side the beam (travel direction dir at p) comes from: true = LEFT.
static func side_left(_seg: Seg, _p: CN, _dir: CN) -> bool:
	return false

# [blocked_left, blocked_right] for a beam through endpoint `which` (1 start, 2 end).
static func blocked_sides(_seg: Seg, _which: int, _dir: CN) -> Array:
	return [false, false]

# Hit records with provenance: `known` = [[point CN, t], ...] exact points the
# beam is constructed through; a root at such a point that is exactly a
# segment endpoint snaps to it. Each record: {t, point, on_segment, at_endpoint}.
static func hit_records(_beam: HPBeam, _seg: Seg, _depart_on: bool, _known: Array = []) -> Array:
	return []
