#include "register_types.hpp"

#include "gd_native_probe.hpp"
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
