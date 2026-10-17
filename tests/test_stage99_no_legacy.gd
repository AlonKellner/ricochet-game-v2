extends GutTest

# Stage 99: the GDScript implementations of the native core are gone. The
# native classes carry their final names, nothing references the Gd* copies or
# the N*-prefixed development names, and every registered extension class is a
# RefCounted (the math layer has no scene-tree types, as Stage 1 demanded of
# the GDScript math layer).

const DELETED_FILES := [
	"res://scripts/math/precision/beam_solver.gd",
	"res://scripts/math/precision/fast_solver.gd",
	"res://scripts/math/precision/beam_tracer.gd",
	"res://scripts/math/precision/precision_ladder.gd",
	"res://scripts/math/precision/error_estimate.gd",
	"res://scripts/math/precision/beam_scene.gd",
	"res://scripts/math/precision/beam_geom.gd",
	"res://scripts/math/precision/beam_effects.gd",
	"res://scripts/math/precision/hp_beam.gd",
	"res://scripts/math/precision/exact.gd",
	"res://scripts/math/precision/big_float.gd",
	"res://scripts/math/precision/cn.gd",
	"res://scripts/math/precision/num_ctx.gd",
	"res://scripts/math/precision/f64_num.gd",
	"res://scripts/math/precision/dual_num.gd",
	"res://scripts/math/point.gd",
	"res://scripts/math/direction.gd",
	"res://scripts/math/ray.gd",
	"res://scripts/math/trace_hit.gd",
	"res://scripts/math/trace_step.gd",
	"res://scripts/math/trace_path.gd",
	"res://scripts/math/segment.gd",
	"res://scripts/math/generalized_circle.gd",
	"res://scripts/math/step_tree_merge.gd",
	"res://scripts/visual/visual_converter.gd",
	"res://scripts/testing/gd_kernels/gd_invariant_checker.gd",
	"res://tools/dump_goldens.gd",
]

# the classes ricochet.gdextension registers (native/gd/register_types.cpp)
const NATIVE_CLASSES := [
	"NumCtx", "Num", "CN", "BigFloat", "F64Num", "DualNum", "Exact",
	"HPBeam", "HPBeamCircle", "BeamGeom", "BeamGeomSeg", "BeamEffects",
	"BeamScene", "BeamSceneSurf",
	"BeamSolver", "BeamSolverEvent", "BeamSolverPlanItem", "BeamSolverAnchorChain",
	"FastSolver", "FastSolverScene", "FastSolverOut",
	"ErrorEstimate", "ErrorEstimateSensitivity", "PrecisionLadder", "PrecisionLadderResult",
	"Point", "Direction", "Ray", "TraceHit", "TraceStep", "TracePath", "BeamTracer", "BeamTracerPair",
	"GeneralizedCircle", "Segment", "VisualConverter", "StepTreeMerge", "ArrowAdvance", "ArrowAdvanceResult", "SurfaceGeom",
	"InvariantKernel", "SweepKernel", "VisibilityRegion",
]

# whole-identifier patterns that must not appear in code (comments stripped)
const FORBIDDEN := {
	"Gd* copy": "(?<![A-Za-z_])Gd[A-Z][A-Za-z]+",
	"N* development name": "(?<![A-Za-z_])N(NumCtx|Num|CN|BigFloat|Exact|HPBeam|BeamGeom|BeamEffects|BeamScene|BeamSolver|FastSolver|ErrorEstimate|PrecisionLadder|BeamTracer|TraceStep|TracePath|TraceHit)(?![A-Za-z_])",
	"inner class of a native": "(?<![A-Za-z_])(BeamSolver\\.(Event|PlanItem|Mode)|FastSolver\\.Scene|BeamScene\\.Surf|PrecisionLadder\\.Result|ErrorEstimate\\.Sensitivity|BeamTracer\\.Pair|HPBeam\\.Circle|BeamGeom\\.Seg)(?![A-Za-z_])",
}

static func _gd_files(dir_path: String, out: Array) -> void:
	var d := DirAccess.open(dir_path)
	if d == null:
		return
	for f in d.get_files():
		if f.ends_with(".gd"):
			out.append(dir_path.path_join(f))
	for sub in d.get_directories():
		if sub == "gut":
			continue
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

func test_gdscript_implementations_are_deleted():
	for f in DELETED_FILES:
		assert_false(FileAccess.file_exists(f), "%s is deleted" % f)
	assert_false(DirAccess.dir_exists_absolute("res://scripts/math/precision"), "scripts/math/precision is gone")
	assert_false(DirAccess.dir_exists_absolute("res://scripts/testing/gd_kernels"), "scripts/testing/gd_kernels is gone")

func test_no_legacy_names():
	var files: Array = []
	for dir in ["res://scripts", "res://tests", "res://tools"]:
		_gd_files(dir, files)
	assert_gt(files.size(), 60)
	var found: Array = []
	for path in files:
		if path == "res://tests/test_stage99_no_legacy.gd":
			continue
		var code := _code_of(FileAccess.open(path, FileAccess.READ).get_as_text())
		for name in FORBIDDEN:
			var m := RegEx.create_from_string(FORBIDDEN[name]).search(code)
			if m != null:
				found.append("%s: %s (%s)" % [path, name, m.get_string()])
	assert_eq(found, [], "no Gd* / N* / inner-class names in scripts, tests or tools")

func test_native_classes_registered_as_refcounted():
	for c in NATIVE_CLASSES:
		assert_true(ClassDB.class_exists(c), "%s is registered" % c)
		assert_true(ClassDB.is_parent_class(c, "RefCounted"), "%s is a RefCounted" % c)
		assert_false(ClassDB.is_parent_class(c, "Node"), "%s is not a Node" % c)

func test_goldens_are_pinned():
	# the GDScript stack that produced them is gone; the files stay as the regression pins ctest replays
	for g in ["bigfloat", "exact", "beam", "fixtures", "corpus", "chain", "sweeps"]:
		assert_true(FileAccess.file_exists("res://bench/goldens/%s.json" % g), "bench/goldens/%s.json" % g)
