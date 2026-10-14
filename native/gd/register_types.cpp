#include "register_types.hpp"

#include "gd_native_probe.hpp"
#include "gd_num.hpp"
#include "gd_scene.hpp"
#include "gd_solver.hpp"
#include "gd_ladder.hpp"
#include "gd_tracer.hpp"
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
    GDREGISTER_CLASS(NNumCtx);
    GDREGISTER_CLASS(NNum);
    GDREGISTER_CLASS(NCN);
    GDREGISTER_CLASS(NBigFloat);
    // Stage 93: scene
    GDREGISTER_CLASS(NBeamSceneSurf);
    GDREGISTER_CLASS(NBeamScene);
    // Stage 92-94: beams and the solver
    GDREGISTER_CLASS(NHPBeam);
    GDREGISTER_CLASS(NBeamSolverPlanItem);
    GDREGISTER_CLASS(NBeamSolverEvent);
    GDREGISTER_CLASS(NBeamSolver);
    GDREGISTER_CLASS(NFastSolverScene);
    GDREGISTER_CLASS(NFastSolverOut);
    GDREGISTER_CLASS(NFastSolver);
    // Stage 95: the ladder
    GDREGISTER_CLASS(NErrorEstimateSensitivity);
    GDREGISTER_CLASS(NErrorEstimate);
    GDREGISTER_CLASS(NPrecisionLadderResult);
    GDREGISTER_CLASS(NPrecisionLadder);
    // Stage 96: the tracer and its data types
    GDREGISTER_CLASS(NDirection);
    GDREGISTER_CLASS(NRay);
    GDREGISTER_CLASS(NTraceHit);
    GDREGISTER_CLASS(NTraceStep);
    GDREGISTER_CLASS(NTracePath);
    GDREGISTER_CLASS(NBeamTracerPair);
    GDREGISTER_CLASS(NBeamTracer);
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
