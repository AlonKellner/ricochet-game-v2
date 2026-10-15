# Ricochet native toolchain (Stage 89).
#
#   ricochet_core  static, Godot-free (namespace rc)        core/
#   ricochet_gd    shared GDExtension, links core+godot-cpp gd/   -> native/bin/
#   rc_tests       doctest executable, registered with ctest tests/
#
# Configure/build from the repo root:
#   cmake -S native -B native/build -DCMAKE_BUILD_TYPE=RelWithDebInfo
#   cmake --build native/build -j
#   ctest --test-dir native/build --output-on-failure
cmake_minimum_required(VERSION 3.25)
project(ricochet_native LANGUAGES CXX)

option(RC_BUILD_TESTS "Build the doctest executable and register ctest tests" ON)
option(RC_BUILD_EXTENSION "Build godot-cpp and the ricochet_gd GDExtension library" ON)
set(RC_SANITIZE "" CACHE STRING "Comma-separated -fsanitize= list for every target, e.g. address,undefined")
set(RC_EXTENSION_OUTPUT_DIR "${CMAKE_SOURCE_DIR}/bin" CACHE PATH
    "Where the GDExtension library is written (ricochet.gdextension points at native/bin)")

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

# ------------------------------------------------------------------ ricochet_gd
if(RC_BUILD_EXTENSION)
    # godot-cpp options (names from extern/godot-cpp/cmake/godotcpp.cmake).
    set(GODOTCPP_TARGET "template_debug" CACHE STRING "godot-cpp target: template_debug|template_release|editor")
    set(GODOTCPP_API_VERSION "4.7" CACHE STRING "Bundled extension_api-<ver>.json to generate bindings from")
    set(GODOTCPP_PRECISION "single" CACHE STRING "Must match the engine build (this project is single precision)")
    set(GODOTCPP_USE_HOT_RELOAD ON CACHE BOOL "ricochet.gdextension sets reloadable = true")
    set(GODOTCPP_SYSTEM_HEADERS ON CACHE BOOL "godot-cpp headers are not held to the rc warning flags")
    add_subdirectory(extern/godot-cpp SYSTEM)

    file(GLOB RC_GD_SOURCES CONFIGURE_DEPENDS gd/*.cpp)
    add_library(ricochet_gd SHARED ${RC_GD_SOURCES})
    target_link_libraries(ricochet_gd PRIVATE ricochet_core godot-cpp)

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
    target_include_directories(rc_tests PRIVATE tests/support)
    # the goldens and fixtures live in the repository (bench/)
    get_filename_component(RC_REPO_ROOT "${CMAKE_SOURCE_DIR}/.." ABSOLUTE)
    target_compile_definitions(rc_tests PRIVATE "RC_REPO_ROOT=\"${RC_REPO_ROOT}\"")
    rc_apply_flags(rc_tests NO_CONVERSION)
    # One binary, several ctest entries so `ctest -j` spreads the work over the
    # cores: the sweep goldens replay in RC_SWEEP_PARTS parts (RC_SWEEP_PART),
    # the Stage 105 tier-independence walks alone, every other case together.
    set(RC_SWEEP_PARTS 6 CACHE STRING "ctest entries the sweep goldens replay is split into")
    set(RC_HEAVY_CASES "goldens: the sweep combos*,Stage 105: on coincidence*")
    add_test(NAME rc_tests COMMAND rc_tests "--test-case-exclude=${RC_HEAVY_CASES}")
    add_test(NAME rc_tests_tier_independence COMMAND rc_tests "--test-case=Stage 105: on coincidence*")
    math(EXPR RC_SWEEP_LAST "${RC_SWEEP_PARTS} - 1")
    foreach(part RANGE ${RC_SWEEP_LAST})
        add_test(NAME rc_tests_sweep_goldens_${part} COMMAND rc_tests "--test-case=goldens: the sweep combos*")
        set_tests_properties(rc_tests_sweep_goldens_${part} PROPERTIES ENVIRONMENT "RC_SWEEP_PART=${part}/${RC_SWEEP_PARTS}")
    endforeach()

    # ricochet_core must stay Godot-free: fails if any file under core/ names godot-cpp.
    add_test(NAME rc_core_no_godot
        COMMAND "${CMAKE_COMMAND}" -DRC_CORE_DIR=${CMAKE_SOURCE_DIR}/core
                -P ${CMAKE_SOURCE_DIR}/tools/check_no_godot.cmake)

    # The API file godot-cpp generated its bindings from must describe the installed binary.
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME rc_api_matches_binary
        COMMAND Python3::Interpreter -I ${CMAKE_SOURCE_DIR}/tools/check_api.py
                ${CMAKE_SOURCE_DIR}/godot-api/extension_api.json
                ${CMAKE_SOURCE_DIR}/extern/godot-cpp/gdextension/extension_api-4-7.json)
endif()
