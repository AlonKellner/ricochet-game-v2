#pragma once

namespace rc {

// Version of the native library. Bumped per migration stage; exposed to
// GDScript through the bindings so a stale native/bin/ is easy to spot.
inline constexpr const char *RC_VERSION = "0.89.0";

} // namespace rc
