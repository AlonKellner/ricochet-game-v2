#pragma once

// Stage 89 smoke-test classes. They port no game code; they exercise every
// binding mechanism later stages depend on, so tests/native/test_extension_loads.gd
// can prove the toolchain end to end from GDScript:
//   static method, enum (BIND_ENUM_CONSTANT), integer constant (BIND_CONSTANT),
//   property with getter/setter (ADD_PROPERTY), returning another registered
//   RefCounted (flat registration standing in for an inner class),
//   PackedFloat64Array in/out, Dictionary in, and the rc error sink.

#include <godot_cpp/classes/ref.hpp>
#include <godot_cpp/classes/ref_counted.hpp>
#include <godot_cpp/variant/dictionary.hpp>
#include <godot_cpp/variant/packed_float64_array.hpp>
#include <godot_cpp/variant/string.hpp>

#include <cstdint>

class NativeProbeChild : public godot::RefCounted {
    GDCLASS(NativeProbeChild, godot::RefCounted)

    double value_ = 0.0;

protected:
    static void _bind_methods();

public:
    void set_value(double p_value);
    double get_value() const;
};

class NativeProbe : public godot::RefCounted {
    GDCLASS(NativeProbe, godot::RefCounted)

    godot::String kind_;

protected:
    static void _bind_methods();

public:
    enum Mode {
        PHYSICAL = 0,
        PLANNED = 1,
    };
    static constexpr int MAX_HITS = 32;

    static int add(int a, int b);
    static godot::String version();

    void set_kind(const godot::String &p_kind);
    godot::String get_kind() const;

    godot::Ref<NativeProbeChild> make_child(double p_value) const;
    godot::PackedFloat64Array doubled(const godot::PackedFloat64Array &p_values) const;
    int64_t key_count(const godot::Dictionary &p_dict) const;
    bool sink_error_test() const;
};

VARIANT_ENUM_CAST(NativeProbe::Mode);
