#include "rc/error.hpp"

#include <cstdio>
#include <cstdlib>
#include <mutex>
#include <string>
#include <utility>

namespace rc {
namespace {

std::mutex &sink_mutex() {
    static std::mutex m;
    return m;
}

ErrorSink &sink_slot() {
    static ErrorSink sink;
    return sink;
}

std::string format_failure(const char *file, int line, const char *expr, const char *msg) {
    std::string out = "RC_ASSERT failed at ";
    out += file;
    out += ':';
    out += std::to_string(line);
    out += ": (";
    out += expr;
    out += ") ";
    out += msg;
    return out;
}

} // namespace

void set_error_sink(ErrorSink sink) {
    const std::lock_guard<std::mutex> lock(sink_mutex());
    sink_slot() = std::move(sink);
}

void report_error(const char *message) {
    ErrorSink sink;
    {
        const std::lock_guard<std::mutex> lock(sink_mutex());
        sink = sink_slot(); // copy: the sink runs outside the lock and may re-enter
    }
    if (sink) {
        sink(message);
    } else {
        std::fprintf(stderr, "%s\n", message);
    }
}

namespace detail {

void assert_abort(const char *file, int line, const char *expr, const char *msg) {
    std::fprintf(stderr, "%s\n", format_failure(file, line, expr, msg).c_str());
    std::fflush(stderr);
    std::abort();
}

void assert_report(const char *file, int line, const char *expr, const char *msg) {
    report_error(format_failure(file, line, expr, msg).c_str());
}

} // namespace detail
} // namespace rc
