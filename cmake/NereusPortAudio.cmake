# NereusSDR-original: checked patches on a build-owned PortAudio copy.
# 2026-10-14, J.J. Boyd (KG4VCF), AI-assisted via OpenAI Codex.
# 2026-10-15, J.J. Boyd (KG4VCF), AI-assisted via Anthropic Claude Code: the
# staging comment now says the Windows copy is CRLF and how the patch reads it.
include_guard(GLOBAL)
if(NOT COMMAND _nereus_apply_vendor_patch)
    include("${CMAKE_CURRENT_LIST_DIR}/NereusRemoteMedia.cmake")
endif()
set(_NEREUS_PORTAUDIO_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

function(_nereus_read_portaudio_lf path out_var)
    file(READ "${path}" _content)
    string(REPLACE "\r\n" "\n" _content "${_content}")
    set(${out_var} "${_content}" PARENT_SCOPE)
endfunction()

function(nereus_stage_portaudio source_dir out_var)
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${_NEREUS_PORTAUDIO_CMAKE_DIR}/patches/portaudio-0001-check-setup-representation.patch")
    # v19.7.0 / 147dd722548358763a8b649b3e4b41dfffbcfbb6. Reject changed
    # arithmetic/struct/allocator preimages rather than silently compiling
    # an unreviewed dependency override. Fetched sources may be shared.
    foreach(_entry IN ITEMS
        "src/common/pa_process.c|742aec8125e62fc7af659181b3dee5c781673c9886b60405f70fd1c0048e7825"
        "src/common/pa_process.h|12148ea1c6ebae3292ead1ec565089be7fb4231d33d8e602050d1c93e01943ad"
        "src/common/pa_util.h|5c0a6ee2134f8019231b5c08ac5174878afb751faa52fea271ad89e144489d6a"
        "src/hostapi/alsa/pa_linux_alsa.c|8ddfc29394321bc57f10a1e7759b379716a4bcc9d519a4f861b74bfaf61da9fd")
        string(REPLACE "|" ";" _parts "${_entry}")
        list(GET _parts 0 _relative)
        list(GET _parts 1 _expected)
        list(APPEND _pinned_paths "${_relative}")
        set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
            "${source_dir}/${_relative}")
        # PortAudio marks C and header files as text. A native Windows clone
        # therefore has CRLF bytes even at the pinned commit.
        _nereus_read_portaudio_lf("${source_dir}/${_relative}" _pinned_content)
        string(SHA256 _actual "${_pinned_content}")
        if(NOT _actual STREQUAL _expected)
            message(FATAL_ERROR "PortAudio pinned preimage mismatch: ${_relative}")
        endif()
    endforeach()

    set(_built "${CMAKE_BINARY_DIR}/_deps/nereus-built-portaudio")
    file(REAL_PATH "${source_dir}" _original)
    # Resolve existing parents without REAL_PATH warnings for a fresh build.
    file(REAL_PATH "${CMAKE_BINARY_DIR}" _binary)
    set(_destination "${_binary}/_deps/nereus-built-portaudio")
    if(EXISTS "${CMAKE_BINARY_DIR}/_deps")
        file(REAL_PATH "${CMAKE_BINARY_DIR}/_deps" _deps)
        set(_destination "${_deps}/nereus-built-portaudio")
    endif()
    if(EXISTS "${_built}")
        file(REAL_PATH "${_built}" _destination)
    endif()
    cmake_path(IS_PREFIX _original "${_destination}" NORMALIZE _destination_in_source)
    cmake_path(IS_PREFIX _destination "${_original}" NORMALIZE _source_in_destination)
    if(_destination_in_source OR _source_in_destination)
        message(FATAL_ERROR "PortAudio staging cannot overlap its source")
    endif()
    # Reconfigure starts from pristine pinned inputs, so the patch applies
    # once without mutating a source override or retaining an old patch.
    file(REMOVE_RECURSE "${_built}")
    file(MAKE_DIRECTORY "${_built}")
    file(COPY "${source_dir}/" DESTINATION "${_built}" PATTERN ".git" EXCLUDE)
    # Apply the checked LF patch to a build copy. On Windows file(WRITE)
    # writes CRLF, so the copy is CRLF there and the patch step reads it as
    # LF (core.autocrlf=input). Keep the fetched source pristine because
    # FetchContent may share that checkout.
    foreach(_relative IN LISTS _pinned_paths)
        _nereus_read_portaudio_lf("${_built}/${_relative}" _staged_content)
        file(WRITE "${_built}/${_relative}" "${_staged_content}")
    endforeach()
    _nereus_apply_vendor_patch("${_built}" "portaudio-0001-check-setup-representation.patch")
    configure_file("${_NEREUS_PORTAUDIO_CMAKE_DIR}/patches/portaudio-bounds.h"
        "${_built}/src/common/portaudio-bounds.h" COPYONLY)
    set(${out_var} "${_built}" PARENT_SCOPE)
endfunction()
