# NereusSDR-original. No fetch, driver, application or device operation.
cmake_minimum_required(VERSION 3.20)
set(CMAKE_BINARY_DIR "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
include("${ROOT}/cmake/NereusPortAudio.cmake")

if(BAD_PREIMAGE)
    nereus_stage_portaudio("${SOURCE}" _bad)
    message(FATAL_ERROR "changed PortAudio preimage was accepted")
endif()

set(_files src/common/pa_process.c src/common/pa_process.h
    src/common/pa_util.h src/hostapi/alsa/pa_linux_alsa.c)
foreach(_file IN LISTS _files)
    file(SHA256 "${SOURCE}/${_file}" _hash)
    string(MAKE_C_IDENTIFIER "${_file}" _key)
    set(_original_${_key} "${_hash}")
endforeach()

nereus_stage_portaudio("${SOURCE}" _built)
foreach(_file IN LISTS _files)
    file(READ "${_built}/${_file}" _staged_text)
    string(FIND "${_staged_text}" "\r" _carriage_return)
    if(NOT _carriage_return EQUAL -1)
        message(FATAL_ERROR "staged PortAudio source is not LF-normalized: ${_file}")
    endif()
endforeach()
file(SHA256 "${_built}/src/common/pa_process.c" _first_common)
file(SHA256 "${_built}/src/hostapi/alsa/pa_linux_alsa.c" _first_alsa)
file(SHA256 "${ROOT}/cmake/patches/portaudio-bounds.h" _helper)
file(SHA256 "${_built}/src/common/portaudio-bounds.h" _staged_helper)
if(NOT _helper STREQUAL _staged_helper)
    message(FATAL_ERROR "staged bounds helper differs from the tested helper")
endif()

# The complete checked patch must be present in the actual build copy.
get_filename_component(_ceiling "${_built}" DIRECTORY)
execute_process(COMMAND "${CMAKE_COMMAND}" -E env
    "GIT_CEILING_DIRECTORIES=${_ceiling}" "${GIT_EXECUTABLE}"
    apply --reverse --check "${ROOT}/cmake/patches/portaudio-0001-check-setup-representation.patch"
    WORKING_DIRECTORY "${_built}" RESULT_VARIABLE _reverse)
if(NOT _reverse EQUAL 0)
    message(FATAL_ERROR "compiled PortAudio copy does not contain the complete patch")
endif()

# Configure again: restore pristine inputs, apply once, obtain identical code.
nereus_stage_portaudio("${SOURCE}" _again)
file(SHA256 "${_again}/src/common/pa_process.c" _second_common)
file(SHA256 "${_again}/src/hostapi/alsa/pa_linux_alsa.c" _second_alsa)
if(NOT _first_common STREQUAL _second_common OR NOT _first_alsa STREQUAL _second_alsa)
    message(FATAL_ERROR "PortAudio reconfigure changed the resulting source")
endif()
foreach(_file IN LISTS _files)
    file(SHA256 "${SOURCE}/${_file}" _hash)
    string(MAKE_C_IDENTIFIER "${_file}" _key)
    if(NOT _hash STREQUAL _original_${_key})
        message(FATAL_ERROR "staging modified original source ${_file}")
    endif()
    get_filename_component(_parent "${WORK}/changed/${_file}" DIRECTORY)
    file(MAKE_DIRECTORY "${_parent}")
    configure_file("${SOURCE}/${_file}" "${WORK}/changed/${_file}" COPYONLY)
endforeach()

execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${ROOT}" "-DSOURCE=${WORK}/changed" "-DWORK=${WORK}/changed"
    -DBAD_PREIMAGE=ON -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE _overlap OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(_overlap EQUAL 0 OR NOT "${_out}${_err}" MATCHES "cannot overlap")
    message(FATAL_ERROR "unexpected source-overlap result: ${_overlap} ${_out}${_err}")
endif()

# A different pin/edited override must fail before copying or patching it.
file(APPEND "${WORK}/changed/src/common/pa_process.c" "\n/* changed preimage */\n")
execute_process(COMMAND "${CMAKE_COMMAND}"
    "-DROOT=${ROOT}" "-DSOURCE=${WORK}/changed" "-DWORK=${WORK}/rejected"
    -DBAD_PREIMAGE=ON -P "${CMAKE_CURRENT_LIST_FILE}"
    RESULT_VARIABLE _bad OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
if(_bad EQUAL 0 OR NOT "${_out}${_err}" MATCHES "pinned preimage mismatch")
    message(FATAL_ERROR "unexpected changed-preimage result: ${_bad} ${_out}${_err}")
endif()

# PortAudio marks C and header sources as text, so a native Windows checkout
# presents CRLF bytes. The pin check must accept that checkout while staging an
# LF-normalized build copy for the checked patch, without changing the source.
if(NOT BAD_PREIMAGE AND NOT CRLF_INPUT)
    set(_crlf_source "${WORK}/crlf-source")
    file(REMOVE_RECURSE "${_crlf_source}")
    file(MAKE_DIRECTORY "${_crlf_source}")
    file(COPY "${SOURCE}/" DESTINATION "${_crlf_source}" PATTERN ".git" EXCLUDE)
    foreach(_file IN LISTS _files)
        file(READ "${_crlf_source}/${_file}" _text)
        string(REPLACE "\r\n" "\n" _text "${_text}")
        string(REPLACE "\n" "\r\n" _text "${_text}")
        file(WRITE "${_crlf_source}/${_file}" "${_text}")
    endforeach()
    execute_process(COMMAND "${CMAKE_COMMAND}"
        "-DROOT=${ROOT}" "-DSOURCE=${_crlf_source}" "-DWORK=${WORK}/crlf-work"
        -DCRLF_INPUT=ON -P "${CMAKE_CURRENT_LIST_FILE}"
        RESULT_VARIABLE _crlf OUTPUT_VARIABLE _out ERROR_VARIABLE _err)
    if(NOT _crlf EQUAL 0)
        message(FATAL_ERROR "Windows-style PortAudio checkout was rejected: ${_crlf} ${_out}${_err}")
    endif()
endif()
message(STATUS "Pinned PortAudio staging, repeatability and source preservation passed")
