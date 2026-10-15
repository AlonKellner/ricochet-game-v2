#include "register_types.hpp"

#include "gd_native_probe.hpp"
#include "gd_num.hpp"
#include "gd_scene.hpp"
#include "gd_solver.hpp"
#include "gd_ladder.hpp"
#include "gd_tracer.hpp"
#include "gd_geom.hpp"
#include "gd_visual.hpp"
#include "gd_invariants.hpp"
#include "rc/error.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>
#include <godot_cpp/variant/string.hpp>
#include <godot_cpp/variant/utility_functions.hpp>

void initialize_ricochet_module(godot::ModuleInitializationLevel p_level) {
    if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    // Release-mode RC_ASSERT failures and rc::report_error surface as Godot
    // errors instead of crashing the editor.
    rc::set_error_sink([](const char *message) {
        godot::UtilityFunctions::push_error(godot::String(message));
    });

    GDREGISTER_CLASS(NativeProbeChild);
    GDREGISTER_CLASS(NativeProbe);
    // Stage 90: scalar tiers
    GDREGISTER_CLASS(NumCtx);
    GDREGISTER_CLASS(Num);
    GDREGISTER_CLASS(CN);
    GDREGISTER_CLASS(BigFloat);
    GDREGISTER_CLASS(F64Num);
    GDREGISTER_CLASS(DualNum);
    // Stage 93: scene
    GDREGISTER_CLASS(BeamSceneSurf);
    GDREGISTER_CLASS(BeamScene);
    // Stage 91-92: exact arithmetic, segments, effect maps
    GDREGISTER_CLASS(Exact);
    GDREGISTER_CLASS(HPBeamCircle);
    GDREGISTER_CLASS(BeamGeomSeg);
    GDREGISTER_CLASS(BeamGeom);
    GDREGISTER_CLASS(BeamEffects);
    // Stage 92-94: beams and the solver
    GDREGISTER_CLASS(HPBeam);
    GDREGISTER_CLASS(BeamSolverPlanItem);
    GDREGISTER_CLASS(BeamSolverEvent);
    GDREGISTER_CLASS(BeamSolverAnchorChain);
    GDREGISTER_CLASS(BeamSolver);
    GDREGISTER_CLASS(FastSolverScene);
    GDREGISTER_CLASS(FastSolverOut);
    GDREGISTER_CLASS(FastSolver);
    // Stage 95: the ladder
    GDREGISTER_CLASS(ErrorEstimateSensitivity);
    GDREGISTER_CLASS(ErrorEstimate);
    GDREGISTER_CLASS(PrecisionLadderResult);
    GDREGISTER_CLASS(PrecisionLadder);
    // Stage 96: the tracer and its data types
    GDREGISTER_CLASS(Point);
    GDREGISTER_CLASS(Direction);
    GDREGISTER_CLASS(Ray);
    GDREGISTER_CLASS(TraceHit);
    GDREGISTER_CLASS(TraceStep);
    GDREGISTER_CLASS(TracePath);
    GDREGISTER_CLASS(BeamTracerPair);
    GDREGISTER_CLASS(BeamTracer);
    // Stage 97: visual / testing kernels
    GDREGISTER_CLASS(GeneralizedCircle);
    GDREGISTER_CLASS(Segment);
    GDREGISTER_CLASS(VisualConverter);
    GDREGISTER_CLASS(StepTreeMerge);
    GDREGISTER_CLASS(ArrowAdvanceResult);
    GDREGISTER_CLASS(ArrowAdvance);
    GDREGISTER_CLASS(SurfaceGeom);
    // Stage 98: the invariant kernel and the native sweep
    GDREGISTER_CLASS(InvariantKernel);
    GDREGISTER_CLASS(SweepKernel);
}

void uninitialize_ricochet_module(godot::ModuleInitializationLevel p_level) {
    if (p_level != godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
        return;
    }
    // The library is reloadable: never leave a sink that points into unloaded code.
    rc::set_error_sink(nullptr);
}

extern "C" {
GDExtensionBool GDE_EXPORT ricochet_library_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
        GDExtensionClassLibraryPtr p_library, GDExtensionInitialization *r_initialization) {
    godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library, r_initialization);

    init_obj.register_initializer(initialize_ricochet_module);
    init_obj.register_terminator(uninitialize_ricochet_module);
    init_obj.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);

    return init_obj.init();
}
}
