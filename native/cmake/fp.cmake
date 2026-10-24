# Compiler flags shared by every rc target.
#
# Floating point: the native solver must stay bit-identical with the shipped
# GDScript FastSolver, which runs on plain IEEE-754 binary64 with one rounding
# per operation. A fused multiply-add rounds a*b+c once, and fast-math licenses
# reassociation; both change results in the last bit, so both are forbidden on
# every rc target (and checked by rc_tests: fma_sensitive).
#
# -fsized-deallocation: `delete p` calls the sized operator delete (Apple clang
# does not by default), so the core's allocator (rc/thread_alloc.hpp) classes a
# freed block by the size it knows instead of asking malloc_size per free.
#
# Warnings are errors. -Wconversion is on by default; pass NO_CONVERSION for
# targets that expand godot-cpp binding macros.
function(rc_apply_flags target)
    cmake_parse_arguments(RC "NO_CONVERSION" "" "" ${ARGN})
    if(MSVC)
        target_compile_options(${target} PRIVATE /fp:precise /W4 /WX /permissive-)
    else()
        target_compile_options(${target} PRIVATE
            -ffp-contract=off -fno-fast-math
            $<$<CONFIG:RelWithDebInfo>:-O3>
            -fsized-deallocation
            -Wall -Wextra -Wpedantic -Werror -Wshadow)
        if(NOT RC_NO_CONVERSION)
            target_compile_options(${target} PRIVATE -Wconversion)
        endif()
    endif()
    set_target_properties(${target} PROPERTIES
        CXX_STANDARD 20
        CXX_STANDARD_REQUIRED ON
        CXX_EXTENSIONS OFF)
endfunction()
