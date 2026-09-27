# no-port-check: NereusSDR-original.
# =================================================================
# tests/scripts/check-libdatachannel-patch.cmake  (NereusSDR)
# =================================================================
# R-R3-49. nereus_patch_libdatachannel_remote_description_first()
# (cmake/NereusRemoteMedia.cmake) compiles libdatachannel from a copy of
# src/peerconnection.cpp with the change in
# cmake/patches/libdatachannel-keep-remote-description-first.cpp. This
# fails when that change stops applying to the pinned source:
#
#   cmake -DREPO=<source dir> -DPC_CPP=<libdatachannel src/peerconnection.cpp>
#         -DWORK=<scratch dir> -P check-libdatachannel-patch.cmake
#
# It configures tests/cmake/libdatachannel-patch over a copy of the pinned
# file and checks that both library targets compile the patched copy, that
# the copy keeps the remote description before the ICE agent takes it and
# no longer has the old order, and that the change carries the pinned
# file's own licence notice byte for byte. Configures only; nothing is
# built or downloaded.
# =================================================================
# Modification history (NereusSDR):
#   2026-09-27: original implementation for NereusSDR by J.J. Boyd
#               (KG4VCF), with AI-assisted implementation via Anthropic
#               Claude Code.
# =================================================================
foreach(_required IN ITEMS REPO PC_CPP WORK)
    if(NOT DEFINED ${_required})
        message(FATAL_ERROR "check-libdatachannel-patch: -D${_required}= is required")
    endif()
endforeach()

file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}/datachannel/src")
file(COPY "${PC_CPP}" DESTINATION "${WORK}/datachannel/src")
file(COPY "${REPO}/tests/cmake/libdatachannel-patch/datachannel/CMakeLists.txt"
     DESTINATION "${WORK}/datachannel")

set(_build "${WORK}/build")
execute_process(
    COMMAND "${CMAKE_COMMAND}" -S "${REPO}/tests/cmake/libdatachannel-patch"
            -B "${_build}" "-DREPO=${REPO}" "-DDC_DIR=${WORK}/datachannel"
    RESULT_VARIABLE _result
    OUTPUT_VARIABLE _out
    ERROR_VARIABLE _err)
if(NOT _result EQUAL 0)
    message(FATAL_ERROR "the libdatachannel change did not apply:\n${_out}\n${_err}")
endif()

set(_patched "${_build}/_deps/nereus-patched/libdatachannel/src/peerconnection.cpp")
if(NOT EXISTS "${_patched}")
    message(FATAL_ERROR "no patched copy at ${_patched}")
endif()
foreach(_target IN ITEMS datachannel datachannel-static)
    string(FIND "${_out}" "nereus-dc-patch ${_target}: ${_patched}" _at)
    if(_at EQUAL -1)
        message(FATAL_ERROR "${_target} does not compile the patched copy:\n${_out}")
    endif()
endforeach()

file(READ "${_patched}" _copy)
set(_kept "\timpl()->processRemoteDescription(description);\n")
set(_taken "\ticeTransport->setRemoteDescription(description); // ICE transport might reject the description\n")
set(_old "\timpl()->processRemoteDescription(std::move(description));\n")
string(FIND "${_copy}" "${_kept}" _kept_at)
string(FIND "${_copy}" "${_taken}" _taken_at)
string(FIND "${_copy}" "${_old}" _old_at)
if(_kept_at EQUAL -1 OR _taken_at EQUAL -1 OR NOT _kept_at LESS _taken_at)
    message(FATAL_ERROR "the patched copy does not keep the remote description before the "
                        "ICE agent takes it")
endif()
if(NOT _old_at EQUAL -1)
    message(FATAL_ERROR "the patched copy still has the old order")
endif()

# The change's notice: the pinned file's own, byte for byte, up to the end
# of its first comment.
file(READ "${PC_CPP}" _pinned)
string(FIND "${_pinned}" "*/\n" _end)
if(_end EQUAL -1)
    message(FATAL_ERROR "the pinned file has no leading notice")
endif()
math(EXPR _length "${_end} + 3")
string(SUBSTRING "${_pinned}" 0 ${_length} _notice)
file(READ "${REPO}/cmake/patches/libdatachannel-keep-remote-description-first.cpp" _change)
string(FIND "${_change}" "${_notice}" _notice_at)
if(NOT _notice_at EQUAL 0)
    message(FATAL_ERROR "the change does not open with the pinned file's own notice")
endif()
message(STATUS "PASS: the libdatachannel change applies to ${PC_CPP}")
