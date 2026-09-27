# no-port-check: NereusSDR-original.
# iPhone app plan Task 28 fix wave (R-IOS-16; review Minor 6): a project
# that compiles a copy of libjuice's agent.c as libjuice's own CMake names
# it (juice: relative, juice-static: absolute) and asks
# nereus_patch_libjuice_turn_release() to swap in the patched copy, the
# directory given as JUICE_GIVEN. Configured by
# tests/scripts/check-libjuice-patch-paths.cmake; never built.
cmake_minimum_required(VERSION 3.20)
project(nereus_libjuice_patch_paths C)
include("${REPO}/cmake/NereusRemoteMedia.cmake")
add_subdirectory("${JUICE_REAL}" juice)
nereus_patch_libjuice_turn_release("${JUICE_GIVEN}")
foreach(_target IN ITEMS juice juice-static)
    get_target_property(_sources ${_target} SOURCES)
    message(STATUS "nereus-patch-paths ${_target}: ${_sources}")
endforeach()
