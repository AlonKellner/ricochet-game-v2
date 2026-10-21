# Third-party code under `native/extern/`

| Directory | What | Version | Source |
|-----------|------|---------|--------|
| `godot-cpp/` | C++ bindings for GDExtension (git submodule) | `master`, pinned to `b8530af7546a3fcae80cef2477526569fd42ffc2` (2026-10-12, v10.x) | https://github.com/godotengine/godot-cpp |
| `doctest/doctest.h` | Single-header C++ test framework | 2.4.12 | https://raw.githubusercontent.com/doctest/doctest/v2.4.12/doctest/doctest.h |
| `nlohmann/json.hpp` | Single-header JSON library (fixture loading in tests; the game layer's level data) | 3.11.3 | https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp |
| `clipper2/` | Polygon clipping (Stage 123: the visibility region drawn as the union of its cells); the engine only (headers, `src/clipper.engine.cpp`), Boost Software License 1.0 (`clipper2/LICENSE`) | 1.4.0 | https://github.com/AngusJohnson/Clipper2/archive/refs/tags/Clipper2_1.4.0.tar.gz (`CPP/Clipper2Lib`) |

godot-cpp has no 4.6/4.7 release branch; `master` is the line that ships the
`extension_api-4-7.json` file and is pinned by commit so builds are
reproducible. To move the pin, `cd native/extern/godot-cpp && git checkout <sha>`
and re-run `native/tools/check_api.py` (ctest `rc_api_matches_binary`).

The single headers are vendored verbatim; do not edit them. `doctest.h` and
`json.hpp` are included from the test targets; `json.hpp` also from `ricochet_game`
(the level data, Stage 115).
