#pragma once

#include <functional>

// Invariant-violation policy for the native library.
//
//   RC_ASSERT(cond, msg)
//     RC_ASSERT_ABORTS != 0 (Debug / ctest builds): prints file:line, the
//       failed expression and msg to stderr, then aborts.
//     RC_ASSERT_ABORTS == 0 (release, what Godot loads): formats the same text,
//       hands it to the error sink and CONTINUES. The bindings set the sink to
//       Godot's push_error, so a violation shows up in the editor, not as a crash.
//
// The build defines RC_ASSERT_ABORTS per configuration (see native/CMakeLists.txt);
// without that definition NDEBUG decides. A translation unit may override the
// macro before including this header, which is how the sink is unit-tested.
//
// Both reporting functions always exist, so mixing policies across translation
// units is well defined. The condition is always evaluated.

#ifndef RC_ASSERT_ABORTS
#ifdef NDEBUG
#define RC_ASSERT_ABORTS 0
#else
#define RC_ASSERT_ABORTS 1
#endif
#endif

namespace rc {

using ErrorSink = std::function<void(const char *message)>;

// Installs the sink used by the non-aborting RC_ASSERT and by report_error.
// An empty sink restores the default, which writes to stderr.
void set_error_sink(ErrorSink sink);

// Routes one message through the current sink.
void report_error(const char *message);

namespace detail {

[[noreturn]] void assert_abort(const char *file, int line, const char *expr, const char *msg);
void assert_report(const char *file, int line, const char *expr, const char *msg);

} // namespace detail
} // namespace rc

#if RC_ASSERT_ABORTS
#define RC_ASSERT(cond, msg)                                                   \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ::rc::detail::assert_abort(__FILE__, __LINE__, #cond, (msg));      \
        }                                                                      \
    } while (0)
#else
#define RC_ASSERT(cond, msg)                                                   \
    do {                                                                       \
        if (!(cond)) {                                                         \
            ::rc::detail::assert_report(__FILE__, __LINE__, #cond, (msg));     \
        }                                                                      \
    } while (0)
#endif
