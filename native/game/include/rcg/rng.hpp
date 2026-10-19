// Stage 116.6: Godot's RandomNumberGenerator (RandomPCG: PCG32 with the
// engine's seeding and float draws), for the sweep positions' fuzz
// (SweepRunner.build_positions). Pinned by values recorded from the engine
// (tests/data/godot_rng.json, rcg_tests).
#pragma once

#include <bit>
#include <cmath>
#include <cstdint>

namespace rcg {

class GodotRng {
public:
	static constexpr std::uint64_t DEFAULT_INC = 1442695040888963407ULL;

	explicit GodotRng(std::uint64_t seed = 0) { set_seed(seed); }

	// RandomNumberGenerator.seed = s (pcg32_srandom_r with the default increment)
	void set_seed(std::uint64_t s) {
		state_ = 0;
		inc_ = (DEFAULT_INC << 1u) | 1u;
		next();
		state_ += s;
		next();
	}
	std::uint32_t randi() { return next(); }
	// RandomPCG::randf: a uniform float in [0, 1) from two draws
	float randf() {
		const std::uint32_t proto_exp_offset = next();
		if (proto_exp_offset == 0) {
			return 0.0f;
		}
		return std::ldexp(float(next() | 0x80000001u), -32 - std::countl_zero(proto_exp_offset));
	}
	// randf_range (real_t: float32)
	float randf_range(float from, float to) { return randf() * (to - from) + from; }

private:
	std::uint32_t next() {
		const std::uint64_t old = state_;
		state_ = old * 6364136223846793005ULL + inc_;
		const auto xorshifted = std::uint32_t(((old >> 18u) ^ old) >> 27u);
		const auto rot = std::uint32_t(old >> 59u);
		return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
	}

	std::uint64_t state_ = 0;
	std::uint64_t inc_ = 0;
};

} // namespace rcg
