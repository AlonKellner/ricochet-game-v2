#include "gd_native_probe.hpp"

#include "rc/error.hpp"
#include "rc/version.hpp"

#include <godot_cpp/core/class_db.hpp>

using godot::ClassDB;
using godot::D_METHOD;
using godot::PropertyInfo;
using godot::Variant;

// ---------------------------------------------------------------- NativeProbeChild

void NativeProbeChild::_bind_methods() {
    ClassDB::bind_method(D_METHOD("set_value", "value"), &NativeProbeChild::set_value);
    ClassDB::bind_method(D_METHOD("get_value"), &NativeProbeChild::get_value);
    ADD_PROPERTY(PropertyInfo(Variant::FLOAT, "value"), "set_value", "get_value");
}

void NativeProbeChild::set_value(double p_value) {
    value_ = p_value;
}

double NativeProbeChild::get_value() const {
    return value_;
}

// --------------------------------------------------------------------- NativeProbe

void NativeProbe::_bind_methods() {
    ClassDB::bind_static_method("NativeProbe", D_METHOD("add", "a", "b"), &NativeProbe::add);
    ClassDB::bind_static_method("NativeProbe", D_METHOD("version"), &NativeProbe::version);

    ClassDB::bind_method(D_METHOD("set_kind", "kind"), &NativeProbe::set_kind);
    ClassDB::bind_method(D_METHOD("get_kind"), &NativeProbe::get_kind);
    ADD_PROPERTY(PropertyInfo(Variant::STRING, "kind"), "set_kind", "get_kind");

    ClassDB::bind_method(D_METHOD("make_child", "value"), &NativeProbe::make_child);
    ClassDB::bind_method(D_METHOD("doubled", "values"), &NativeProbe::doubled);
    ClassDB::bind_method(D_METHOD("key_count", "dict"), &NativeProbe::key_count);
    ClassDB::bind_method(D_METHOD("sink_error_test"), &NativeProbe::sink_error_test);

    BIND_ENUM_CONSTANT(PHYSICAL);
    BIND_ENUM_CONSTANT(PLANNED);
    BIND_CONSTANT(MAX_HITS);
}

int NativeProbe::add(int a, int b) {
    return a + b;
}

godot::String NativeProbe::version() {
    return godot::String(rc::RC_VERSION);
}

void NativeProbe::set_kind(const godot::String &p_kind) {
    kind_ = p_kind;
}

godot::String NativeProbe::get_kind() const {
    return kind_;
}

godot::Ref<NativeProbeChild> NativeProbe::make_child(double p_value) const {
    godot::Ref<NativeProbeChild> child;
    child.instantiate();
    child->set_value(p_value);
    return child;
}

godot::PackedFloat64Array NativeProbe::doubled(const godot::PackedFloat64Array &p_values) const {
    godot::PackedFloat64Array out;
    const int64_t n = p_values.size();
    out.resize(n);
    const double *src = p_values.ptr();
    double *dst = out.ptrw();
    for (int64_t i = 0; i < n; ++i) {
        dst[i] = 2.0 * src[i]; // exact for every finite double, keeps the sign of zero
    }
    return out;
}

int64_t NativeProbe::key_count(const godot::Dictionary &p_dict) const {
    return p_dict.size();
}

bool NativeProbe::sink_error_test() const {
    // Goes through the sink installed in initialize_ricochet_module, so it must
    // show up as a push_error on the Godot side. Deliberately not RC_ASSERT:
    // that would abort a Debug build of the library, by design.
    rc::report_error("NativeProbe.sink_error_test: intentional test error routed through the rc error sink");
    return true;
}
