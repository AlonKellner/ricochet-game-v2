// Stage 115.2: the level data (rc-level/1, docs/LEVEL_FORMAT.md) for GDScript,
// read natively: Godot never parses a level's numbers (its decimal parsing is
// not correctly rounded). RcgLevel.parse checks the text against the schema
// (rcg::parse_level) and the builder (rcg::build_level), then hands GDScript
// the data as Variants.
#pragma once

#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/string.hpp>

class RcgLevel : public godot::RefCounted {
	GDCLASS(RcgLevel, godot::RefCounted)

protected:
	static void _bind_methods();

public:
	// {"level": the data (Dictionary, as in the file)} or {"error": String}
	static godot::Dictionary parse(const godot::String &text);
};
