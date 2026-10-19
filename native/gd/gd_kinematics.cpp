#include "gd_kinematics.hpp"

#include "gd_common.hpp"
#include "rcg/kinematics.hpp"

#include <godot_cpp/core/class_db.hpp>

#include <algorithm>

using namespace godot;

namespace {
rc::V2 rv(const Vector2 &v) { return rc::V2{v.x, v.y}; }
} // namespace

void RcgKinematics::_bind_methods() {
	ClassDB::bind_static_method("RcgKinematics", D_METHOD("step", "body", "input", "gravity", "defs", "flags", "colliders"), &RcgKinematics::step);
	ClassDB::bind_static_method("RcgKinematics", D_METHOD("radius"), &RcgKinematics::radius);
	ClassDB::bind_static_method("RcgKinematics", D_METHOD("speed"), &RcgKinematics::speed);
	ClassDB::bind_static_method("RcgKinematics", D_METHOD("jump_velocity"), &RcgKinematics::jump_velocity);
}

double RcgKinematics::radius() { return rcg::kinematics::RADIUS; }
double RcgKinematics::speed() { return rcg::kinematics::SPEED; }
double RcgKinematics::jump_velocity() { return rcg::kinematics::JUMP_VELOCITY; }

Dictionary RcgKinematics::step(const Dictionary &body, const Dictionary &input, const Vector2 &gravity, const Array &defs,
		const Dictionary &flags, const PackedVector2Array &colliders) {
	namespace k = rcg::kinematics;
	const rc::Flags f = rcgd::flags_from_dict(flags);
	const std::vector<rc::SurfaceDef> sd = rcgd::defs_from_array(defs);
	std::vector<k::Collider> cs;
	for (size_t i = 0; i < sd.size(); i++) {
		const Dictionary d = defs[int64_t(i)];
		const rc::SurfaceDef &s = sd[i];
		if (!bool(d.get("player_solid", false)) || s.pre_transform) {
			continue;
		}
		bool present = true;
		for (const rc::PresenceCond &c : s.presence) {
			const rc::FlagValue *v = f.get(c.key);
			const std::string key = v ? rc::flag_key(*v) : std::string("DEFAULT");
			present = present && std::find(c.values.begin(), c.values.end(), key) != c.values.end();
		}
		if (!present) {
			continue; // absent: it does not block (TDD_22)
		}
		rc::arc::SegGeom g = rc::arc::SegGeom::make(rc::V2::of(s.sx, s.sy), rc::V2::of(s.ex, s.ey),
			s.has_via ? rc::V2::of(s.vx, s.vy) : rc::V2::inf(), s.full);
		if (s.has_circle) {
			g = rc::arc::SegGeom::full_from_carrier(rc::arc::from_circle(rc::V2::of(s.cx, s.cy), s.radius));
		} else if (s.line) {
			g.carrier = rc::arc::straight_carrier(g.s, g.e);
		}
		cs.push_back(k::collider_of(g));
	}
	for (int64_t i = 0; i + 1 < colliders.size(); i += 2) {
		cs.push_back(k::segment_collider(rv(colliders[i]), rv(colliders[i + 1])));
	}
	k::Body b{rv(body["pos"]), rv(body["vel"]), bool(body["on_floor"])};
	k::Input in;
	in.axis_x = float(input.get("axis_x", 0.0));
	in.axis_y = float(input.get("axis_y", 0.0));
	in.jump = bool(input.get("jump", false));
	k::step(b, in, rv(gravity), cs);
	Dictionary out;
	out["pos"] = Vector2(b.pos.x, b.pos.y);
	out["vel"] = Vector2(b.vel.x, b.vel.y);
	out["on_floor"] = b.on_floor;
	return out;
}
