# Ricochet native toolchain (Stage 89).
#
#   ricochet_core  static, Godot-free (namespace rc)        core/
#   ricochet_game  static, engine-free (namespace rcg)      game/  (Stage 115: levels, ...)
#   rc_capi        the C API (rc_api.h), static and shared       capi/
#   ricochet_gd    shared GDExtension: the Godot host on the C API  hosts/godot/ -> native/bin/
#   rc_tests       doctest executable, registered with ctest tests/
#   rcg_tests      doctest executable for the game layer       tests/game/
#   rc_tool        the headless host (data pipelines)           hosts/headless/
#
# Configure/build from the repo root:
#   make core      # rc_tests alone (native/build-core, RC_BUILD_EXTENSION=OFF)
#   make check     # core + ctest (AREA=solver|vis|goldens|all, or TC=<doctest filter>)
#   make native    # the extension (native/build, RC_BUILD_TESTS=OFF) for Godot
#   (docs/WORKFLOW.md)
cmake_minimum_required(VERSION 3.25)
project(ricochet_native LANGUAGES CXX)

option(RC_BUILD_TESTS "Build the doctest executable and register ctest tests" ON)
option(RC_BUILD_EXTENSION "Build godot-cpp and the ricochet_gd GDExtension library" ON)
set(RC_SANITIZE "" CACHE STRING "Comma-separated -fsanitize= list for every target, e.g. address,undefined")
set(RC_EXTENSION_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH
    "Where the GDExtension library is written (ricochet.gdextension points at native/bin)")

# ccache when installed (the Makefile passes nothing: a tree configured before
# ccache was installed keeps its rules). Content-keyed, so a branch switch or a
# stash that restores a file compiles nothing twice.
if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
    find_program(RC_CCACHE ccache)
    if(RC_CCACHE)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${RC_CCACHE}" CACHE STRING "compiler launcher (ccache)")
    endif()
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

get_property(RC_IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT RC_IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "Build type" FORCE)
endif()

# Sanitizers apply to everything configured below this line, godot-cpp included,
# so instrumented and uninstrumented code never mix in one binary.
if(RC_SANITIZE)
    add_compile_options(-fsanitize=${RC_SANITIZE} -fno-omit-frame-pointer)
    add_link_options(-fsanitize=${RC_SANITIZE})
    # godot-cpp's defs.hpp refuses to compile under a sanitizer unless the
    # matching <X>SAN_ENABLED macro says it was intentional.
    string(REPLACE "," ";" RC_SANITIZERS "${RC_SANITIZE}")
    foreach(san IN LISTS RC_SANITIZERS)
        if(san STREQUAL "address")
            add_compile_definitions(ASAN_ENABLED)
        elseif(san STREQUAL "undefined")
            add_compile_definitions(UBSAN_ENABLED)
        elseif(san STREQUAL "leak")
            add_compile_definitions(LSAN_ENABLED)
        elseif(san STREQUAL "memory")
            add_compile_definitions(MSAN_ENABLED)
        elseif(san STREQUAL "thread")
            add_compile_definitions(TSAN_ENABLED)
        endif()
    endforeach()
endif()

include(cmake/fp.cmake)

# ---------------------------------------------------------------- ricochet_core
file(GLOB RC_CORE_SOURCES CONFIGURE_DEPENDS core/src/*.cpp)
add_library(ricochet_core STATIC ${RC_CORE_SOURCES})
target_include_directories(ricochet_core PUBLIC core/include)
set_target_properties(ricochet_core PROPERTIES POSITION_INDEPENDENT_CODE ON)
# RC_ASSERT policy (core/include/rc/error.hpp): abort in Debug, report through
# the error sink and continue otherwise. PUBLIC so every consumer agrees with core.
target_compile_definitions(ricochet_core PUBLIC "RC_ASSERT_ABORTS=$<IF:$<CONFIG:Debug>,1,0>")
rc_apply_flags(ricochet_core)

# ---------------------------------------------------------------- ricochet_game
# Stage 115: the engine-free game layer (docs/TDD_20_ENGINE_AGNOSTIC_DATA.md):
# the level data (rc-level/1) and its builder. JSON through the vendored
# nlohmann (native/extern).
file(GLOB RC_GAME_SOURCES CONFIGURE_DEPENDS game/src/*.cpp)
add_library(ricochet_game STATIC ${RC_GAME_SOURCES})
target_include_directories(ricochet_game PUBLIC game/include)
target_include_directories(ricochet_game SYSTEM PUBLIC extern)
target_link_libraries(ricochet_game PUBLIC ricochet_core)
set_target_properties(ricochet_game PROPERTIES POSITION_INDEPENDENT_CODE ON)
rc_apply_flags(ricochet_game)

# Stage 118: the C API (rc_api.h) -- the game for any engine or host: static
# (rc_tool, the Godot adapter) and shared (ctypes / P/Invoke hosts)
file(GLOB RC_CAPI_SOURCES CONFIGURE_DEPENDS capi/src/*.cpp)
add_library(rc_capi STATIC ${RC_CAPI_SOURCES})
target_include_directories(rc_capi PUBLIC capi/include)
target_link_libraries(rc_capi PRIVATE ricochet_game)
set_target_properties(rc_capi PROPERTIES POSITION_INDEPENDENT_CODE ON)
rc_apply_flags(rc_capi)
add_library(rc_capi_shared SHARED ${RC_CAPI_SOURCES})
target_include_directories(rc_capi_shared PUBLIC capi/include)
target_link_libraries(rc_capi_shared PRIVATE ricochet_game)
set_target_properties(rc_capi_shared PROPERTIES OUTPUT_NAME ricochet_capi CXX_VISIBILITY_PRESET hidden)
rc_apply_flags(rc_capi_shared)

# Stage 115: the headless host -- the data pipelines without an engine (Stage
# 118: its replay plays the game through the C API alone)
add_executable(rc_tool hosts/headless/rc_tool.cpp hosts/headless/replay_capi.cpp)
target_link_libraries(rc_tool PRIVATE ricochet_game rc_capi)
rc_apply_flags(rc_tool)

# ------------------------------------------------------------------ ricochet_gd
if(RC_BUILD_EXTENSION)
    # godot-cpp options (names from extern/godot-cpp/cmake/godotcpp.cmake).
    set(GODOTCPP_TARGET "template_debug" CACHE STRING "godot-cpp target: template_debug|template_release|editor")
    set(GODOTCPP_API_VERSION "4.7" CACHE STRING "Bundled extension_api-<ver>.json to generate bindings from")
    set(GODOTCPP_PRECISION "single" CACHE STRING "Must match the engine build (this project is single precision)")
    set(GODOTCPP_USE_HOT_RELOAD ON CACHE BOOL "hot reload support in the bindings (ricochet.gdextension has reloadable = false: the library is loaded once)")
    set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "godot-cpp headers are not held to the rc warning flags")
    add_subdirectory(extern/godot-cpp SYSTEM)

    # Stage 119: the Godot host (hosts/godot), on the C API alone
    file(GLOB RC_GD_SOURCES CONFIGURE_DEPENDS hosts/godot/*.cpp)
    add_library(ricochet_gd SHARED ${RC_GD_SOURCES})
    target_link_libraries(ricochet_gd PRIVATE rc_capi godot-cpp)

    get_target_property(GODOTCPP_SUFFIX godot-cpp GODOTCPP_SUFFIX)
    set_target_properties(ricochet_gd PROPERTIES
        CXX_VISIBILITY_PRESET hidden
        VISIBILITY_INLINES_HIDDEN ON
        POSITION_INDEPENDENT_CODE ON
        PREFIX "lib"
        OUTPUT_NAME "ricochet_gd${GODOTCPP_SUFFIX}"
        # the genex wrapper keeps multi-config generators from appending a config dir
        LIBRARY_OUTPUT_DIRECTORY "$<1:${RC_EXTENSION_OUTPUT_DIR}>"
        RUNTIME_OUTPUT_DIRECTORY "$<1:${RC_EXTENSION_OUTPUT_DIR}>"
        ARCHIVE_OUTPUT_DIRECTORY "$<1:${RC_EXTENSION_OUTPUT_DIR}>"
        PDB_OUTPUT_DIRECTORY "$<1:${RC_EXTENSION_OUTPUT_DIR}>")
    # -Wconversion stays off here: godot-cpp's binding templates narrow freely
    # inside macros that expand in our translation units.
    rc_apply_flags(ricochet_gd NO_CONVERSION)
endif()

# --------------------------------------------------------------------- tests
if(RC_BUILD_TESTS)
    enable_testing()

    file(GLOB RC_TEST_SOURCES CONFIGURE_DEPENDS tests/*.cpp)
    add_executable(rc_tests ${RC_TEST_SOURCES})
    target_link_libraries(rc_tests PRIVATE ricochet_core)
    # vendored single headers: <doctest/doctest.h>, <nlohmann/json.hpp>
    target_include_directories(rc_tests SYSTEM PRIVATE extern)
    target_include_directories(rc_tests PRIVATE tests/support game/include)
    # the goldens and fixtures live in the repository (bench/)
    get_filename_component(RC_REPO_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
    target_compile_definitions(rc_tests PRIVATE "RC_REPO_ROOT=\"${RC_REPO_ROOT}\"")
    rc_apply_flags(rc_tests NO_CONVERSION)
    # One binary, several ctest entries so `ctest -j` spreads the work over the
    # cores: the sweep goldens replay in RC_SWEEP_PARTS parts (RC_SWEEP_PART),
    # the Stage 105 tier-independence walks alone, every other case together.
    set(RC_SWEEP_PARTS 6 CACHE STRING "ctest entries the sweep goldens replay is split into")
    set(RC_HEAVY_CASES "goldens: the sweep combos*,Stage 105: on coincidence*,Stage 106: pops at known points*,visibility:*,sweeps:*")
    add_test(NAME rc_tests COMMAND rc_tests "--test-case-exclude=${RC_HEAVY_CASES}")
    # the two random-scene walks (the ASan critical path) in RC_CASE_PARTS parts
    # each (RC_CASE_PART: the k-th scene of every n, the scenes drawn as unsharded)
    set(RC_CASE_PARTS 4 CACHE STRING "ctest entries the Stage 105/106 random-scene cases are split into")
    math(EXPR RC_CASE_LAST "${RC_CASE_PARTS} - 1")
    foreach(part RANGE ${RC_CASE_LAST})
        add_test(NAME rc_tests_tier_independence_${part} COMMAND rc_tests "--test-case=Stage 105: on coincidence*")
        add_test(NAME rc_tests_pop_tiers_${part} COMMAND rc_tests "--test-case=Stage 106: pops at known points*")
        set_tests_properties(rc_tests_tier_independence_${part} rc_tests_pop_tiers_${part}
            PROPERTIES ENVIRONMENT "RC_CASE_PART=${part}/${RC_CASE_PARTS}")
    endforeach()
    # Stage 110: the visibility region against its definition on the shipped
    # levels. The two level loops (the plans' pin, the structure checks) run in
    # RC_VIS_PARTS parts over the level files (RC_VIS_PART); the rest together.
    set(RC_VIS_PARTS 6 CACHE STRING "ctest entries the visibility level loops are split into")
    set(RC_VIS_LEVEL_CASES "visibility: plans' regions*,visibility: the regions' structure checks*")
    # the slower single cases alone (each runs check_visibility on a costly region)
    set(RC_VIS_ALONE "visibility: VIS-REGULAR allows*" "visibility: the region and the traces agree with the oracle's labels*")
    set(RC_VIS_REGIONS_CASE "visibility: the regions' digest matches*")
    set(RC_VIS_EXCLUDE "${RC_VIS_LEVEL_CASES},${RC_VIS_REGIONS_CASE}")
    set(i 0)
    foreach(c IN LISTS RC_VIS_ALONE)
        add_test(NAME rc_tests_visibility_alone_${i} COMMAND rc_tests "--test-case=${c}")
        string(APPEND RC_VIS_EXCLUDE ",${c}")
        math(EXPR i "${i} + 1")
    endforeach()
    add_test(NAME rc_tests_visibility COMMAND rc_tests "--test-case=visibility:*" "--test-case-exclude=${RC_VIS_EXCLUDE}")
    math(EXPR RC_VIS_LAST "${RC_VIS_PARTS} - 1")
    foreach(part RANGE ${RC_VIS_LAST})
        add_test(NAME rc_tests_visibility_levels_${part} COMMAND rc_tests "--test-case=${RC_VIS_LEVEL_CASES}")
        set_tests_properties(rc_tests_visibility_levels_${part} PROPERTIES ENVIRONMENT "RC_VIS_PART=${part}/${RC_VIS_PARTS}")
    endforeach()
    # Workflow W4: the regions' digest golden (bench/goldens/regions.json) in parts
    foreach(part RANGE ${RC_VIS_LAST})
        add_test(NAME rc_tests_regions_${part} COMMAND rc_tests "--test-case=${RC_VIS_REGIONS_CASE}")
        set_tests_properties(rc_tests_regions_${part} PROPERTIES ENVIRONMENT "RC_VIS_PART=${part}/${RC_VIS_PARTS}")
    endforeach()
    math(EXPR RC_SWEEP_LAST "${RC_SWEEP_PARTS} - 1")
    foreach(part RANGE ${RC_SWEEP_LAST})
        add_test(NAME rc_tests_sweep_goldens_${part} COMMAND rc_tests "--test-case=goldens: the sweep combos*")
        set_tests_properties(rc_tests_sweep_goldens_${part} PROPERTIES ENVIRONMENT "RC_SWEEP_PART=${part}/${RC_SWEEP_PARTS}")
    endforeach()

    # Workflow W2: the GUT sweeps' manifests (bench/sweeps) natively, the players
    # in parts (RC_SWEEPS_PART); one cast thread per region (the parts are the
    # parallelism)
    set(RC_SWEEPS_INV_PARTS 6 CACHE STRING "ctest entries the invariant sweeps run in")
    set(RC_SWEEPS_VIS_PARTS 12 CACHE STRING "ctest entries the visibility sweeps run in")
    math(EXPR RC_SWEEPS_INV_LAST "${RC_SWEEPS_INV_PARTS} - 1")
    foreach(part RANGE ${RC_SWEEPS_INV_LAST})
        add_test(NAME rc_sweeps_inv_${part} COMMAND rc_tests "--test-case=sweeps: the invariant*")
        set_tests_properties(rc_sweeps_inv_${part} PROPERTIES ENVIRONMENT "RC_SWEEPS_PART=${part}/${RC_SWEEPS_INV_PARTS};RC_VIS_THREADS=1")
    endforeach()
    math(EXPR RC_SWEEPS_VIS_LAST "${RC_SWEEPS_VIS_PARTS} - 1")
    foreach(part RANGE ${RC_SWEEPS_VIS_LAST})
        add_test(NAME rc_sweeps_vis_${part} COMMAND rc_tests "--test-case=sweeps: the visibility*")
        set_tests_properties(rc_sweeps_vis_${part} PROPERTIES ENVIRONMENT "RC_SWEEPS_PART=${part}/${RC_SWEEPS_VIS_PARTS};RC_VIS_THREADS=1")
    endforeach()

    # The engine-free layers name no game engine (Stage 119: core, game, the C API, the headless host).
    foreach(layer core game capi hosts/headless)
        string(REPLACE "/" "_" layer_name "${layer}")
        add_test(NAME ${layer_name}_no_engine
            COMMAND "${CMAKE_COMMAND}" -DRC_CORE_DIR=${CMAKE_SOURCE_DIR}/${layer}
                    -P ${CMAKE_SOURCE_DIR}/tools/check_no_engine.cmake)
    endforeach()
    # the Godot adapter knows the game through rc_api.h alone
    add_test(NAME godot_adapter_includes
        COMMAND "${CMAKE_COMMAND}" -DRC_ADAPTER_DIR=${CMAKE_SOURCE_DIR}/hosts/godot
                -P ${CMAKE_SOURCE_DIR}/tools/check_adapter_includes.cmake)

    # Stage 115: the game layer's tests (make check AREA=game)
    file(GLOB RCG_TEST_SOURCES CONFIGURE_DEPENDS tests/game/*.cpp)
    add_executable(rcg_tests ${RCG_TEST_SOURCES})
    target_link_libraries(rcg_tests PRIVATE ricochet_game)
    target_include_directories(rcg_tests SYSTEM PRIVATE extern)
    target_include_directories(rcg_tests PRIVATE tests/support)
    target_compile_definitions(rcg_tests PRIVATE "RC_REPO_ROOT=\"${RC_REPO_ROOT}\"")
    rc_apply_flags(rcg_tests NO_CONVERSION)
    add_test(NAME rcg_tests COMMAND rcg_tests "--test-case-exclude=sessions:*")
    # Stage 116.5: the native session against the session replay golden, in parts
    set(RCG_SESSIONS_PARTS 6 CACHE STRING "ctest entries the session replay runs in")
    math(EXPR RCG_SESSIONS_LAST "${RCG_SESSIONS_PARTS} - 1")
    foreach(part RANGE ${RCG_SESSIONS_LAST})
        add_test(NAME rcg_sessions_${part} COMMAND rcg_tests "--test-case=sessions:*")
        set_tests_properties(rcg_sessions_${part} PROPERTIES ENVIRONMENT "RCG_SESSIONS_PART=${part}/${RCG_SESSIONS_PARTS}")
    endforeach()
    # Stage 118: the C API -- a C11 host compiles against rc_api.h and plays a
    # level; every session replayed through the C API equals the golden
    enable_language(C)
    add_executable(capi_c_smoke tests/capi/smoke.c)
    set_target_properties(capi_c_smoke PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED ON C_EXTENSIONS OFF)
    target_compile_options(capi_c_smoke PRIVATE -Wall -Wextra -Wpedantic -Werror)
    target_link_libraries(capi_c_smoke PRIVATE rc_capi_shared)
    target_compile_definitions(capi_c_smoke PRIVATE "RC_REPO_ROOT=\"${RC_REPO_ROOT}\"")
    add_test(NAME capi_c_smoke COMMAND capi_c_smoke)
    set(CAPI_SESSIONS_PARTS 6 CACHE STRING "ctest entries the C API session replay runs in")
    math(EXPR CAPI_SESSIONS_LAST "${CAPI_SESSIONS_PARTS} - 1")
    foreach(part RANGE ${CAPI_SESSIONS_LAST})
        add_test(NAME capi_sessions_${part} COMMAND rc_tool replay ${RC_REPO_ROOT}/data/sessions ${RC_REPO_ROOT}/data/levels
                 --check ${RC_REPO_ROOT}/bench/sessions --shard ${part}/${CAPI_SESSIONS_PARTS})
    endforeach()

    # the level data is in its canonical spelling (rc_tool fmt)
    add_test(NAME rcg_levels_canonical COMMAND rc_tool fmt --check ${RC_REPO_ROOT}/data/levels)

    # The API file godot-cpp generated its bindings from must describe the installed binary.
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    # Stage 119: the GUT retirement ledger holds; the Godot host's GDScript stays under budget
    add_test(NAME gut_ledger COMMAND Python3::Interpreter -I ${RC_REPO_ROOT}/tools/gut_ledger_check.py)
    add_test(NAME gdscript_budget COMMAND Python3::Interpreter -I ${RC_REPO_ROOT}/tools/check_gdscript_budget.py)
    add_test(NAME godot_api_matches_binary
        COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/check_api.py
                ${CMAKE_SOURCE_DIR}/godot-api/extension_api.json
                ${CMAKE_SOURCE_DIR}/extern/godot-cpp/gdextension/extension_api-4-7.json)
endif()
