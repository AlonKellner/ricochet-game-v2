// Stage 116: the game's 2D affine transform (Godot's Transform2D in the
// single-precision build, the game's float32 contract): three float32
// columns -- x, y (the basis) and the origin. Spawned surfaces are placed
// through it (SpawnFrame, SurfaceSpec.mapped). Every operation is float32 as
// the engine computes it; pinned by values recorded from the engine
// (tests/data/godot_xf2.json, rcg_tests).
#pragma once

#include "rc/trace_types.hpp"

#include <cmath>

namespace rc {

struct Xf2 {
	V2 x{1.0f, 0.0f};
	V2 y{0.0f, 1.0f};
	V2 origin;

	static Xf2 identity() { return Xf2{}; }
	static Xf2 of(const V2 &px, const V2 &py, const V2 &p_origin) { return Xf2{px, py, p_origin}; }
	// Transform2D(rotation, position): the angle is a float32 (real_t), cos/sin in float32
	static Xf2 rotation(float rot, const V2 &pos) {
		const float cr = std::cos(rot);
		const float sr = std::sin(rot);
		return Xf2{V2{cr, sr}, V2{-sr, cr}, pos};
	}

	float tdotx(const V2 &v) const { return x.x * v.x + y.x * v.y; }
	float tdoty(const V2 &v) const { return x.y * v.x + y.y * v.y; }
	V2 basis_xform(const V2 &v) const { return V2{tdotx(v), tdoty(v)}; }
	// `xf * v`
	V2 xform(const V2 &v) const { return V2{tdotx(v), tdoty(v)} + origin; }
	float determinant() const { return x.x * y.y - x.y * y.x; }

	// `a * b`: Transform2D::operator*= (the origin first, then the basis)
	Xf2 operator*(const Xf2 &o) const {
		Xf2 t = *this;
		t.origin = xform(o.origin);
		t.x = V2{tdotx(o.x), tdoty(o.x)};
		t.y = V2{tdotx(o.y), tdoty(o.y)};
		return t;
	}

	Xf2 affine_inverse() const {
		Xf2 t = *this;
		const float det = determinant();
		const float idet = 1.0f / det;
		std::swap(t.x.x, t.y.y);
		t.x = V2{t.x.x * idet, t.x.y * -idet};
		t.y = V2{t.y.x * -idet, t.y.y * idet};
		t.origin = t.basis_xform(-origin);
		return t;
	}

	bool operator==(const Xf2 &o) const { return x == o.x && y == o.y && origin == o.origin; }
};

} // namespace rc
