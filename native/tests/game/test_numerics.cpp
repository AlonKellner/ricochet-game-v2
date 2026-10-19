// Stage 116.2: the game's float32 numerics against values recorded from the
// engine (tests/data/godot_xf2.json, scripts/testing/record_numerics.gd):
// Transform2D (rc::Xf2) and the vector expressions of the spawn frame,
// including where a float64 GDScript scalar meets a float32 vector (the
// scalar is rounded to float32 first).
#include "rc/xf2.hpp"

#include <doctest/doctest.h>
#include <nlohmann/json.hpp>

#include <cstring>
#include <fstream>
#include <string>

namespace {

using json = nlohmann::json;
using rc::V2;
using rc::Xf2;

double hexf(const json &j) {
	const std::string h = j.get<std::string>();
	REQUIRE(h.size() == 17);
	unsigned char b[8];
	for (int i = 0; i < 8; i++) {
		b[i] = (unsigned char)std::stoul(h.substr(size_t(1 + 2 * i), 2), nullptr, 16);
	}
	double d;
	std::memcpy(&d, b, 8);
	return d;
}

V2 vec(const json &j) { return V2{float(hexf(j[0])), float(hexf(j[1]))}; }
Xf2 xf(const json &j) { return Xf2::of(vec(j[0]), vec(j[1]), vec(j[2])); }

std::string show(const V2 &v) { return std::to_string(v.x) + "," + std::to_string(v.y); }
std::string show(const Xf2 &t) { return "[" + show(t.x) + " | " + show(t.y) + " | " + show(t.origin) + "]"; }

} // namespace

TEST_CASE("numerics: Transform2D and the spawn frame's vector expressions, as the engine computes them") {
	std::ifstream in(std::string(RC_REPO_ROOT) + "/tests/data/godot_xf2.json");
	REQUIRE(in.good());
	const json doc = json::parse(in);
	int n = 0;
	for (const json &c : doc["cases"]) {
		CAPTURE(n);
		const Xf2 a = xf(c["a"]);
		const Xf2 b = xf(c["b"]);
		const V2 p = vec(c["p"]), u = vec(c["u"]), w = vec(c["w"]), pos = vec(c["pos"]);
		const Xf2 ra = Xf2::rotation(float(hexf(c["rot"])), pos);
		CHECK_MESSAGE(ra == a, show(ra) << " vs " << show(a));
		CHECK(a.xform(p) == vec(c["a_xform_p"]));
		CHECK(b.xform(p) == vec(c["b_xform_p"]));
		CHECK(b.basis_xform(p) == vec(c["b_basis_p"]));
		CHECK(double(b.determinant()) == hexf(c["det_b"]));
		CHECK_MESSAGE(a * b == xf(c["a_mul_b"]), show(a * b));
		CHECK_MESSAGE(b * a == xf(c["b_mul_a"]), show(b * a));
		CHECK_MESSAGE(b.affine_inverse() == xf(c["inv_b"]), show(b.affine_inverse()) << " vs " << show(xf(c["inv_b"])));
		CHECK(a.affine_inverse() == xf(c["inv_a"]));
		// SpawnFrame.frame then SpawnTemplate.place's offset: Transform2D(u, rot90(u), p) * Transform2D(0.0, p / 100.0)
		const V2 off = p / float(100.0);
		CHECK(Xf2::of(u, V2{-u.y, u.x}, p) * Xf2::rotation(0.0f, off) == xf(c["frame"]));
		// 2.0 * u.dot(w) * w - u: the float32 dot widened, doubled in float64, rounded to float32 for the vector product
		const float s = float(2.0 * double(u.dot(w)));
		CHECK(w * s - u == vec(c["reflect"]));
		CHECK((p + pos) / 2.0f == vec(c["mid"]));
		CHECK((p - pos).normalized() == vec(c["norm"]));
		CHECK(double((p - pos).cross(u)) == hexf(c["cross"]));
		n++;
	}
	CHECK(n == 200);
}
