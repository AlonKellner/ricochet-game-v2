# Third-party code under `native/extern/`

| Directory | What | Version | Source |
|-----------|------|---------|--------|
| `godot-cpp/` | C++ bindings for GDExtension (git submodule) | `master`, pinned to `b8530af7546a3fcae80cef2477526569fd42ffc2` (2026-10-12, v10.x) | https://github.com/godotengine/godot-cpp |
| `doctest/doctest.h` | Single-header C++ test framework | 2.4.12 | https://raw.githubusercontent.com/doctest/doctest/v2.4.12/doctest/doctest.h |
| `nlohmann/json.hpp` | Single-header JSON library (fixture loading in tests) | 3.11.3 | https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp |

godot-cpp has no 4.6/4.7 release branch; `master` is the line that ships the
`extension_api-4-7.json` file and is pinned by commit so builds are
reproducible. To move the pin, `cd native/extern/godot-cpp && git checkout <sha>`
and re-run `native/tools/check_api.py` (ctest `rc_api_matches_binary`).

The single headers are vendored verbatim; do not edit them. `doctest.h` and
`json.hpp` are included only from the `rc_tests` target.
