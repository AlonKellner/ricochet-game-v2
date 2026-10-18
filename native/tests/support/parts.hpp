// RC_<NAME>_PART=k/n: a loop takes its k-th item of every n (ctest runs the
// parts as separate entries, in parallel; an unset or malformed value is the
// whole loop). The sweeps, the visibility level loops and the random-scene
// cases share the rule.
#pragma once

#include <cstdio>
#include <cstdlib>

namespace rc_test {

struct Part {
	long k = 0;
	long n = 1;
	bool takes(long index) const { return n <= 1 || index % n == k; }
	bool whole() const { return n <= 1; }
};

inline Part part_of(const char *env_name) {
	Part p;
	if (const char *s = std::getenv(env_name)) {
		if (std::sscanf(s, "%ld/%ld", &p.k, &p.n) != 2 || p.n < 1 || p.k < 0 || p.k >= p.n) {
			p = Part{};
		}
	}
	return p;
}

} // namespace rc_test
