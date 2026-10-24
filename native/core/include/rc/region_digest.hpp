// The region's digest: one hash of what a region draws and decides -- its
// loops' and whiskers' edges (the exact bits of a, via, b; kind, member index,
// surface, inclusive flags, arc) and its cell count. bench/goldens/regions.json
// pins it per region (test_visibility.cpp); rc_tool regions reports it per plan,
// so a timing run doubles as the bit-exactness check of a whole workload.
#pragma once

#include "rc/visibility.hpp"

#include <bit>
#include <cstdint>

namespace rc::vis {

inline uint64_t digest_mix(uint64_t h, uint64_t v) { return (h ^ v) * 1099511628211ULL; }

inline uint64_t result_hash(const Result &r) {
	auto bits = [](double x) { return std::bit_cast<uint64_t>(x); };
	auto edge_hash = [&](uint64_t h, const Edge &e) {
		for (double x : {e.a.x, e.a.y, e.via.x, e.via.y, e.b.x, e.b.y}) h = digest_mix(h, bits(x));
		h = digest_mix(h, uint64_t(int(e.kind)) | uint64_t(uint32_t(e.member)) << 8 | uint64_t(uint32_t(e.surface)) << 40);
		return digest_mix(h, uint64_t(e.inclusive) | uint64_t(e.incl_a) << 1 | uint64_t(e.arc) << 2);
	};
	uint64_t h = digest_mix(14695981039346656037ULL, uint64_t(r.supported));
	h = digest_mix(h, r.cells.size());
	for (const Loop &l : r.loops) {
		h = digest_mix(h, l.edges.size());
		for (const Edge &e : l.edges) h = edge_hash(h, e);
	}
	for (const Edge &e : r.whiskers) h = edge_hash(h, e);
	return h;
}

} // namespace rc::vis
