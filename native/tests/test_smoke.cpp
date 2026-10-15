#include <doctest/doctest.h>

#include "rc/fp_check.hpp"
#include "rc/version.hpp"

#include <cmath>
#include <string_view>

TEST_CASE("rc version string is non-empty") {
    CHECK_FALSE(std::string_view(rc::RC_VERSION).empty());
}

TEST_CASE("fma_sensitive is not contracted into a fused multiply-add") {
    // a*b = (1 + 2^-52)(1 - 2^-52) = 1 - 2^-104 exactly. That is not a double:
    // the nearest double is 1.0, so a product rounded on its own becomes 1.0 and
    // 1.0 + (-1) == 0.0. A fused multiply-add keeps the exact product and rounds
    // only once, giving -2^-104. The toolchain must produce the two-rounding
    // answer, because that is what the GDScript FastSolver gets.
    const double a = 1.0 + std::ldexp(1.0, -52);
    const double b = 1.0 - std::ldexp(1.0, -52);
    const double c = -1.0;

    CHECK(rc::fma_sensitive(a, b, c) == 0.0);

    // Sanity: the triple really is FMA-sensitive.
    CHECK(std::fma(a, b, c) == -std::ldexp(1.0, -104));
}
