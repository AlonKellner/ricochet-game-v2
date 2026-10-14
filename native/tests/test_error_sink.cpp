// Compiles RC_ASSERT in its release (non-aborting) form regardless of the build
// configuration, so the sink path is covered by Debug and ASan runs as well.
#undef RC_ASSERT_ABORTS
#define RC_ASSERT_ABORTS 0
#include "rc/error.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

TEST_CASE("release-mode RC_ASSERT reports through the sink and continues") {
    std::vector<std::string> got;
    rc::set_error_sink([&got](const char *message) { got.emplace_back(message); });

    RC_ASSERT(1 + 1 == 2, "never fires");
    CHECK(got.empty());

    RC_ASSERT(1 + 1 == 3, "arithmetic broke");
    const bool reached_after_failure = true; // execution continued past the failed assert
    CHECK(reached_after_failure);
    REQUIRE(got.size() == 1);
    CHECK(got[0].find("test_error_sink.cpp") != std::string::npos);
    CHECK(got[0].find("1 + 1 == 3") != std::string::npos);
    CHECK(got[0].find("arithmetic broke") != std::string::npos);

    rc::set_error_sink(nullptr);
}

TEST_CASE("report_error uses the installed sink; an empty sink is allowed") {
    std::string last;
    rc::set_error_sink([&last](const char *message) { last = message; });
    rc::report_error("hello sink");
    CHECK(last == "hello sink");

    rc::set_error_sink(nullptr);
    rc::report_error("goes to stderr, must not crash");
}
