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
	// Stage 116.3, a differential probe: the level built natively, every
	// surface's behaviour under the flags -- [{id, present, sides: [left, right]}],
	// a side {active (scene defs), landing (writes), link ([from, outgoing])}
	static godot::Array probe_model(const godot::String &text, const godot::Dictionary &flags);
	// Stage 116.4: the level's pieces natively -- {pieces: [[id, index, start, end, via, span]], crossings, portal_crossings}
	static godot::Dictionary probe_pieces(const godot::String &text);
	// Stage 116.4: two segments ({start, end, via} or {center, radius}) natively --
	// {crossings: [[point, ta, tb, enters]], params: [param(a, p) for p in points], span_a, span_b}
	static godot::Dictionary probe_crossings(const godot::Dictionary &a, const godot::Dictionary &b, const godot::PackedVector2Array &points);
	// Stage 116.4: the landing fold natively on the level's world as built --
	// hits as TracePath.hits ({surface_id, side, fired, point, tangent, flags_before}) ->
	// {flags, generations, placed: the encoded [{channel, generation, spec (SessionDigest.data)}]}
	static godot::Dictionary probe_fold(const godot::String &text, const godot::Array &hits, const godot::Dictionary &final_flags,
			const godot::Dictionary &generations);
};
