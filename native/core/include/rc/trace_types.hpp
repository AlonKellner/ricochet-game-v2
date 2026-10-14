// The traced path as the game displays it (Stage 96): the port of
// scripts/math/trace_step.gd, trace_path.gd, trace_hit.gd, point.gd,
// direction.gd and ray.gd. Geometry is float32 (Godot's Vector2 in this
// single-precision build); every float32 operation is written out so results
// are bit-identical to GDScript (Godot's Vector2 arithmetic is plain,
// unfused float math: verified on this build).
#pragma once

#include <cmath>
#include <limits>
#include <optional>
#include <vector>

namespace rc {

// Godot Vector2 (float32 components) with the operations the tracer uses.
struct V2 {
	float x = 0.0f;
	float y = 0.0f;

	static constexpr float INF_F = std::numeric_limits<float>::infinity();
	static V2 inf() { return V2{INF_F, INF_F}; }
	static V2 of(double px, double py) { return V2{float(px), float(py)}; }

	bool operator==(const V2 &o) const { return x == o.x && y == o.y; }
	bool operator!=(const V2 &o) const { return !(*this == o); }
	bool is_inf() const { return std::isinf(x) || std::isinf(y); }
	V2 operator+(const V2 &o) const { return V2{x + o.x, y + o.y}; }
	V2 operator-(const V2 &o) const { return V2{x - o.x, y - o.y}; }
	V2 operator*(float s) const { return V2{x * s, y * s}; }
	V2 operator/(float s) const { return V2{x / s, y / s}; }
	V2 operator-() const { return V2{-x, -y}; }
	float dot(const V2 &o) const { return x * o.x + y * o.y; }
	float cross(const V2 &o) const { return x * o.y - y * o.x; }
	float length_squared() const { return x * x + y * y; }
	float length() const { return std::sqrt(x * x + y * y); }
	// Godot: l = x*x + y*y; if l != 0: l = sqrt(l); x /= l; y /= l
	V2 normalized() const {
		float l = x * x + y * y;
		if (l != 0.0f) {
			l = std::sqrt(l);
			return V2{x / l, y / l};
		}
		return *this;
	}
	float distance_squared_to(const V2 &o) const { return (x - o.x) * (x - o.x) + (y - o.y) * (y - o.y); }
	float distance_to(const V2 &o) const { return std::sqrt((x - o.x) * (x - o.x) + (y - o.y) * (y - o.y)); }
	// Godot: atan2(y, x) in float
	float angle() const { return std::atan2(y, x); }
	// Godot: from + (to - from) * weight, per component, in float
	V2 lerp(const V2 &to, float w) const { return V2{x + (to.x - x) * w, y + (to.y - y) * w}; }
	// Godot: sine = sin(by), cosine = cos(by) in float
	V2 rotated(float by) const {
		const float sine = std::sin(by);
		const float cosine = std::cos(by);
		return V2{x * cosine - y * sine, x * sine + y * cosine};
	}
};
inline V2 operator*(float s, const V2 &v) { return v * s; }

// Godot Rect2 (float32): position + size; grow and has_point as the engine computes them.
struct Rect2f {
	V2 position;
	V2 size;
	static Rect2f of(double x, double y, double w, double h) { return Rect2f{V2::of(x, y), V2::of(w, h)}; }
	V2 end() const { return position + size; }
	Rect2f grow(float by) const {
		Rect2f g = *this;
		g.position.x -= by;
		g.position.y -= by;
		g.size.x += by * 2;
		g.size.y += by * 2;
		return g;
	}
	bool has_point(const V2 &p) const {
		if (p.x < position.x) {
			return false;
		}
		if (p.y < position.y) {
			return false;
		}
		if (p.x >= (position.x + size.x)) {
			return false;
		}
		if (p.y >= (position.y + size.y)) {
			return false;
		}
		return true;
	}
};

// GDScript fposmod on floats (doubles).
inline double fposmod(double x, double y) {
	double value = std::fmod(x, y);
	if ((value < 0 && y > 0) || (value > 0 && y < 0)) {
		value += y;
	}
	value += 0.0;
	return value;
}

// A ray from an origin through a second point (the aim: player -> cursor).
struct Ray {
	V2 origin;
	V2 dir_start;
	V2 dir_end;
	V2 to_vector() const { return dir_end - dir_start; }
	V2 to_normalized() const {
		const V2 v = to_vector();
		if (v.length_squared() == 0.0f) {
			return V2{};
		}
		return v.normalized();
	}
};

// Side convention of the game (Side.Value).
enum class Side : int { Left = 0, Right = 1 };

// A hit of a beam on a surface's segment (the hit step's record).
struct TraceHit {
	double t = 0.0;
	V2 point;
	int surface_id = -1; // the segment hit (the game Surface's id), -1 none
	Side side = Side::Left;
	bool on_segment = false;
};

enum class StepType : int { Aligned = 0, AlignedPostPlanned = 1, DivergedPhysical = 2, DivergedPlanned = 3, DivergedPostPlanned = 4 };

constexpr int IDENTITY_FRAME = 0;

struct TraceStep {
	V2 start;
	V2 end;
	V2 via; // for steps to/from infinity: the travel direction, not a point
	int frame_id = IDENTITY_FRAME;
	std::optional<TraceHit> hit;
	std::optional<Ray> ray;
	bool is_arc_step = false;
	StepType type = StepType::Aligned;
	int surface_id = -1;
	int hit_side = -1;
	bool hit_on_segment = false;
	bool after_portal = false;
	bool ghost = false;
	double est_error = -1.0;
	int precision_bits = 0;

	// TraceStep.new(start, end, frame_id, hit, ray, via, is_arc): a zero via is the midpoint.
	static TraceStep make(V2 p_start, V2 p_end, int p_frame_id, std::optional<TraceHit> p_hit, std::optional<Ray> p_ray, V2 p_via, bool p_is_arc) {
		TraceStep s;
		s.start = p_start;
		s.end = p_end;
		s.frame_id = p_frame_id;
		s.hit = std::move(p_hit);
		s.ray = std::move(p_ray);
		s.via = p_via != V2{} ? p_via : (p_start + p_end) / 2.0f;
		s.is_arc_step = p_is_arc;
		return s;
	}

	TraceStep with_type(StepType new_type) const {
		TraceStep copy = make(start, end, frame_id, hit, ray, via, is_arc_step);
		copy.type = new_type;
		copy.copy_meta_from(*this);
		return copy;
	}

	void copy_meta_from(const TraceStep &o) {
		surface_id = o.surface_id;
		hit_side = o.hit_side;
		hit_on_segment = o.hit_on_segment;
		after_portal = o.after_portal;
		est_error = o.est_error;
		precision_bits = o.precision_bits;
		ghost = o.ghost;
	}
};

struct TracePath {
	std::vector<TraceStep> steps;
	std::vector<int> targets_hit; // surface ids, in first-hit order
	int cursor_index = -1;
	int hit_count = 0;
};

} // namespace rc
