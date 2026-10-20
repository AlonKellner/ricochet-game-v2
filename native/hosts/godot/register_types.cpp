// The GDExtension's entry point (ricochet.gdextension: ricochet_library_init):
// the Godot host's classes (ricochet_game.hpp). The game itself is behind
// rc_api.h.
#include "rc_api.h"
#include "ricochet_game.hpp"

#include <godot_cpp/core/class_db.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

namespace {

void initialize(godot::ModuleInitializationLevel level) {
	if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		rc_godot::register_adapter();
	}
}

void uninitialize(godot::ModuleInitializationLevel level) {
	if (level == godot::MODULE_INITIALIZATION_LEVEL_SCENE) {
		rc_set_log(nullptr, nullptr); // no log sink pointing into an unloaded library
	}
}

} // namespace

extern "C" {
GDExtensionBool GDE_EXPORT ricochet_library_init(GDExtensionInterfaceGetProcAddress get_proc_address, GDExtensionClassLibraryPtr library,
		GDExtensionInitialization *initialization) {
	godot::GDExtensionBinding::InitObject init(get_proc_address, library, initialization);
	init.register_initializer(initialize);
	init.register_terminator(uninitialize);
	init.set_minimum_library_initialization_level(godot::MODULE_INITIALIZATION_LEVEL_SCENE);
	return init.init();
}
}
