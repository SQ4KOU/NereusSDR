# no-port-check: NereusSDR-original.
# =================================================================
# tests/scripts/check-libjuice-patch-paths.cmake  (NereusSDR)
# =================================================================
# iPhone app plan Task 28 fix wave (R-IOS-16; the safety review's Minor 6).
# nereus_patch_libjuice_turn_release() finds libjuice's agent.c among the
# juice targets' sources by the file it names, so a libjuice directory
# given with a trailing slash, a `..` or through a symbolic link (as an
# offline kit's FETCHCONTENT_SOURCE_DIR may be) still gets the change.
#
#   cmake -DREPO=<source dir> -DAGENT_C=<libjuice src/agent.c>
#         -DWORK=<scratch dir> -P check-libjuice-patch-paths.cmake
#
# Configures tests/cmake/libjuice-patch-paths once per spelling of one
# copy of agent.c and checks both targets compile the patched copy.
# Configures only; nothing is built or downloaded.
# =================================================================
# Modification history (NereusSDR):
#   2026-09-26: original implementation for NereusSDR by J.J. Boyd
#               (KG4VCF), with AI-assisted implementation via Anthropic
#               Claude Code.
# =================================================================
foreach(_required IN ITEMS REPO AGENT_C WORK)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "check-libjuice-patch-paths: -D${_required}= is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/juice/src" "${WORK}/elsewhere")
file(COPY "${AGENT_C}" DESTINATION "${WORK}/juice/src")
file(COPY "${REPO}/tests/cmake/libjuice-patch-paths/juice/CMakeLists.txt"
     DESTINATION "${WORK}/juice")
set(_spellings "${WORK}/juice" "${WORK}/juice/" "${WORK}/elsewhere/../juice")
if(NOT CMAKE_HOST_WIN32)
    file(CREATE_LINK "${WORK}/juice" "${WORK}/link" SYMBOLIC)
    list(APPEND _spellings "${WORK}/link")
endif()

set(_failures 0)
set(_case 0)
foreach(_given IN LISTS _spellings)
    math(EXPR _case "${_case} + 1")
    set(_build "${WORK}/build-${_case}")
    execute_process(
        COMMAND "${CMAKE_COMMAND}" -S "${REPO}/tests/cmake/libjuice-patch-paths"
                -B "${_build}" "-DREPO=${REPO}" "-DJUICE_REAL=${WORK}/juice"
                "-DJUICE_GIVEN=${_given}"
        RESULT_VARIABLE _result
        OUTPUT_VARIABLE _out
        ERROR_VARIABLE _err)
    set(_patched "${_build}/_deps/nereus-patched/libjuice/src/agent.c")
    set(_ok FALSE)
    if(_result EQUAL 0 AND EXISTS "${_patched}")
        string(FIND "${_out}" "nereus-patch-paths juice: ${_patched}" _juice)
        string(FIND "${_out}" "nereus-patch-paths juice-static: ${_patched}" _static)
        if(NOT _juice EQUAL -1 AND NOT _static EQUAL -1)
            set(_ok TRUE)
        endif()
    endif()
    if(_ok)
        message(STATUS "PASS: ${_given}")
    else()
        math(EXPR _failures "${_failures} + 1")
        message(STATUS "FAIL: ${_given}\n${_out}\n${_err}")
    endif()
endforeach()
if(_failures GREATER 0)
    message(FATAL_ERROR "${_failures} spelling(s) of the libjuice directory were not patched")
endif()
