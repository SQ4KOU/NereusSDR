# =================================================================
# cmake/NereusRemoteMedia.cmake  (NereusSDR)
# =================================================================
#
# no-port-check: NereusSDR-original. Remote-daemon R3 Task 1.
#
# Fetch the audited libdatachannel v0.24.5 source and the exact transitive
# revisions recorded by that tree. Codeload archives are used because they
# are content-addressed here and do not require github.com git transport.
#
# Scope boundary: this R3 build selects libjuice, which is appropriate for
# direct host ICE and TURN/UDP. Pinned libdatachannel v0.24.5 explicitly
# rejects TURN TCP and TURN TLS in src/impl/icetransport.cpp when USE_NICE is
# false. This is therefore not R5 relay acceptance and does not prove the
# required TLS/TCP 443 fallback. IMediaTransport keeps signalling and media
# backend details outside its API so a later libnice or relay adapter can be
# selected without changing session callers. DTLS-SRTP over TURN/UDP must not
# be described as client-to-relay TLS transport.
#
# Modification history (NereusSDR):
#   2026-09-26: iPhone app plan Task 28 (R-IOS-16): libjuice gives its TURN
#               allocations back when an agent is destroyed
#               (nereus_patch_libjuice_turn_release(), the change in
#               cmake/patches/libjuice-release-turn-allocations.c). J.J. Boyd
#               (KG4VCF), with AI-assisted implementation via Anthropic
#               Claude Code.
#   2026-09-26: Task 28 fix wave (review Minor 6): agent.c is found among
#               the targets' sources by the file it names, not its spelling.
#               J.J. Boyd (KG4VCF), with AI-assisted implementation via
#               Anthropic Claude Code.
#   2026-09-27: R-R3-49: libdatachannel keeps a remote description before
#               its ICE agent takes it (nereus_patch_libdatachannel_remote_
#               description_first(), the change in cmake/patches/
#               libdatachannel-keep-remote-description-first.cpp). J.J. Boyd
#               (KG4VCF), with AI-assisted implementation via Anthropic
#               Claude Code.
#
# =================================================================

include(FetchContent)

set(_NEREUS_REMOTE_MEDIA_CMAKE_DIR "${CMAKE_CURRENT_LIST_DIR}")

# iPhone app plan Task 28 (R-IOS-16): libjuice 3c40a354 never gives a TURN
# allocation back; the relay holds it (and one of its per-user quota places)
# for TURN_LIFETIME after the connection ends. This compiles libjuice's
# targets from a copy of src/agent.c in the build tree that, when an agent
# is destroyed, sends each allocation a Refresh with LIFETIME 0 (RFC 8656
# section 7.2). The fetched source is left as it is. The copy is rewritten
# only when its content changes, so a configure does not rebuild libjuice.
# A pinned libjuice whose agent.c no longer has the two places the change
# goes stops the configure rather than build without it.
function(nereus_patch_libjuice_turn_release juice_dir)
    set(_source "${juice_dir}/src/agent.c")
    file(READ "${_source}" _agent)
    file(READ "${_NEREUS_REMOTE_MEDIA_CMAKE_DIR}/patches/libjuice-release-turn-allocations.c"
         _release)
    set(_destroy "void agent_destroy(juice_agent_t *agent) {\n")
    set(_conn "\tif (agent->conn_impl) {\n\t\tconn_destroy(agent);\n\t}\n")
    foreach(_anchor IN ITEMS _destroy _conn)
        string(FIND "${_agent}" "${${_anchor}}" _first)
        string(FIND "${_agent}" "${${_anchor}}" _last REVERSE)
        if(_first EQUAL -1 OR NOT _first EQUAL _last)
            message(FATAL_ERROR
                "libjuice ${_source} does not have exactly one place for the TURN "
                "release change (${_anchor}); update "
                "cmake/patches/libjuice-release-turn-allocations.c for this libjuice.")
        endif()
    endforeach()
    string(REPLACE "${_conn}" "\tnereus_release_turn_allocations(agent);\n\n${_conn}"
           _agent "${_agent}")
    string(REPLACE "${_destroy}" "${_release}${_destroy}" _agent "${_agent}")

    set(_patched "${CMAKE_BINARY_DIR}/_deps/nereus-patched/libjuice/src/agent.c")
    set(_old "")
    if(EXISTS "${_patched}")
        file(READ "${_patched}" _old)
    endif()
    if(NOT _old STREQUAL _agent)
        file(WRITE "${_patched}" "${_agent}")
    endif()

    foreach(_target IN ITEMS juice juice-static)
        if(TARGET ${_target})
            get_target_property(_sources ${_target} SOURCES)
            get_target_property(_target_dir ${_target} SOURCE_DIR)
            # Matched by the file each entry names, not its spelling: a
            # FETCHCONTENT_SOURCE_DIR given with a trailing slash, a `..` or
            # through a symbolic link names the same agent.c differently.
            file(REAL_PATH "${_source}" _source_real)
            set(_index -1)
            set(_position 0)
            foreach(_entry IN LISTS _sources)
                if(_index EQUAL -1 AND NOT _entry MATCHES "^\\$<")
                    file(REAL_PATH "${_entry}" _entry_real BASE_DIRECTORY "${_target_dir}")
                    if(_entry_real STREQUAL _source_real)
                        set(_index ${_position})
                    endif()
                endif()
                math(EXPR _position "${_position} + 1")
            endforeach()
            if(_index EQUAL -1)
                message(FATAL_ERROR "libjuice target ${_target} does not compile ${_source}")
            endif()
            list(REMOVE_AT _sources ${_index})
            list(INSERT _sources ${_index} "${_patched}")
            set_property(TARGET ${_target} PROPERTY SOURCES ${_sources})
        endif()
    endforeach()
endfunction()

# R-R3-49: libdatachannel v0.24.5's PeerConnection::setRemoteDescription()
# gives the ICE agent the remote description before it keeps it for the
# DTLS fingerprint check, and a handshake that reaches the check in between
# fails. On a busy computer an offerer taking its answer lost that race
# (the Core's control channel and media alike: "DTLS alert: unknown CA").
# This compiles libdatachannel's targets from a copy of src/peerconnection.cpp
# in the build tree with the two calls in the other order (the change and
# its notice are cmake/patches/libdatachannel-keep-remote-description-first.cpp).
# The fetched source is left as it is; the copy is rewritten only when its
# content changes. A pinned libdatachannel without exactly one place for the
# change stops the configure rather than build without it.
function(nereus_patch_libdatachannel_remote_description_first dc_dir)
    set(_source "${dc_dir}/src/peerconnection.cpp")
    file(READ "${_source}" _pc)
    file(READ "${_NEREUS_REMOTE_MEDIA_CMAKE_DIR}/patches/libdatachannel-keep-remote-description-first.cpp"
         _first)
    set(_anchor "\ticeTransport->setRemoteDescription(description); // ICE transport might reject the description\n\n\timpl()->processRemoteDescription(std::move(description));\n")
    string(FIND "${_pc}" "${_anchor}" _at)
    string(FIND "${_pc}" "${_anchor}" _last REVERSE)
    if(_at EQUAL -1 OR NOT _at EQUAL _last)
        message(FATAL_ERROR
            "libdatachannel ${_source} does not have exactly one place for the remote "
            "description change; update "
            "cmake/patches/libdatachannel-keep-remote-description-first.cpp for this "
            "libdatachannel.")
    endif()
    string(REPLACE "${_anchor}" "${_first}" _pc "${_pc}")

    set(_patched "${CMAKE_BINARY_DIR}/_deps/nereus-patched/libdatachannel/src/peerconnection.cpp")
    set(_old "")
    if(EXISTS "${_patched}")
        file(READ "${_patched}" _old)
    endif()
    if(NOT _old STREQUAL _pc)
        file(WRITE "${_patched}" "${_pc}")
    endif()

    file(REAL_PATH "${_source}" _source_real)
    set(_swapped 0)
    foreach(_target IN ITEMS datachannel datachannel-static)
        if(TARGET ${_target})
            get_target_property(_sources ${_target} SOURCES)
            get_target_property(_target_dir ${_target} SOURCE_DIR)
            set(_index -1)
            set(_position 0)
            foreach(_entry IN LISTS _sources)
                if(_index EQUAL -1 AND NOT _entry MATCHES "^\\$<")
                    file(REAL_PATH "${_entry}" _entry_real BASE_DIRECTORY "${_target_dir}")
                    if(_entry_real STREQUAL _source_real)
                        set(_index ${_position})
                    endif()
                endif()
                math(EXPR _position "${_position} + 1")
            endforeach()
            if(_index EQUAL -1)
                message(FATAL_ERROR
                    "libdatachannel target ${_target} does not compile ${_source}")
            endif()
            list(REMOVE_AT _sources ${_index})
            list(INSERT _sources ${_index} "${_patched}")
            set_property(TARGET ${_target} PROPERTY SOURCES ${_sources})
            math(EXPR _swapped "${_swapped} + 1")
        endif()
    endforeach()
    if(_swapped EQUAL 0)
        message(FATAL_ERROR "no libdatachannel target to compile ${_patched} into")
    endif()
endfunction()

function(nereus_add_remote_media_dependency)
    if(TARGET LibDataChannel::LibDataChannelStatic)
        return()
    endif()

    FetchContent_Declare(nereus_libdatachannel
        URL https://codeload.github.com/paullouisageneau/libdatachannel/tar.gz/refs/tags/v0.24.5
        URL_HASH SHA256=454537c3cd526bed935d847bb2dff4046f266eef84d43b2a5f2f2f293c0026f4
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR __nereus_no_add_subdirectory)
    FetchContent_Declare(nereus_plog
        URL https://codeload.github.com/SergiusTheBest/plog/tar.gz/94899e0b926ac1b0f4750bfbd495167b4a6ae9ef
        URL_HASH SHA256=92a08bce559b5f28aa88d3fd9071567414b9f43a83fe2a05a6dd14f1da536072
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR __nereus_no_add_subdirectory)
    FetchContent_Declare(nereus_usrsctp
        URL https://codeload.github.com/paullouisageneau/usrsctp/tar.gz/fec583d54493f879d2ae44a743423bf8a04371ab
        URL_HASH SHA256=e5c114afe73c9a0ec419fab5f5b3f63f3ce57b09b90d06ea85e63dca8aed3e7c
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR __nereus_no_add_subdirectory)
    FetchContent_Declare(nereus_libjuice
        URL https://codeload.github.com/paullouisageneau/libjuice/tar.gz/3c40a3545b6b1b62c7adee7f8f2bd58aa290afd6
        URL_HASH SHA256=a6b1d55338ea12adc0177eaafd9521ac0101b6a8716c71024a668f4896fd6b7c
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR __nereus_no_add_subdirectory)
    FetchContent_Declare(nereus_json
        URL https://codeload.github.com/nlohmann/json/tar.gz/55f93686c01528224f448c19128836e7df245f72
        URL_HASH SHA256=67f4cdd9ca930c9c1e130af4a437c7fc98fab77a2846fc2d2a14b4943831f8ef
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR __nereus_no_add_subdirectory)
    FetchContent_Declare(nereus_libsrtp
        URL https://codeload.github.com/cisco/libsrtp/tar.gz/24b3bf8f19b6f5ab4cd2bcceb4f4064efca86fd5
        URL_HASH SHA256=063478e368d7cd13d04a908d152a46f90bfa728c4b324be6d413b0b92728207c
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR __nereus_no_add_subdirectory)

    FetchContent_MakeAvailable(
        nereus_libdatachannel nereus_plog nereus_usrsctp
        nereus_libjuice nereus_json nereus_libsrtp)

    foreach(_dep IN ITEMS plog usrsctp libjuice json libsrtp)
        file(REMOVE_RECURSE "${nereus_libdatachannel_SOURCE_DIR}/deps/${_dep}")
        file(MAKE_DIRECTORY "${nereus_libdatachannel_SOURCE_DIR}/deps/${_dep}")
        file(COPY "${nereus_${_dep}_SOURCE_DIR}/"
             DESTINATION "${nereus_libdatachannel_SOURCE_DIR}/deps/${_dep}")
    endforeach()

    # These are function-scope normal variables. They configure only the
    # nested project and leave the parent cache, BUILD_SHARED_LIBS, and later
    # dependencies unchanged.
    set(BUILD_SHARED_LIBS OFF)
    set(BUILD_SHARED_DEPS_LIBS OFF)
    set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
    set(NO_WEBSOCKET ON)
    set(NO_MEDIA OFF)
    set(NO_EXAMPLES ON)
    set(NO_TESTS ON)
    set(PREFER_SYSTEM_LIB OFF)
    set(USE_SYSTEM_SRTP OFF)
    set(USE_SYSTEM_JUICE OFF)
    set(USE_SYSTEM_USRSCTP OFF)
    set(USE_SYSTEM_PLOG OFF)
    set(USE_SYSTEM_JSON OFF)
    set(USE_GNUTLS OFF)
    set(USE_MBEDTLS OFF)
    set(USE_NICE OFF)
    set(PLOG_INSTALL OFF)
    set(JSON_Install OFF)
    set(LIBSRTP_TEST_APPS OFF)
    set(ENABLE_WARNINGS_AS_ERRORS OFF)
    set(BUILD_WITH_WARNINGS OFF)
    set(sctp_build_programs OFF)
    set(sctp_werror OFF)
    set(NO_SERVER ON)

    add_subdirectory(
        "${nereus_libdatachannel_SOURCE_DIR}"
        "${CMAKE_BINARY_DIR}/_deps/nereus_libdatachannel-build"
        EXCLUDE_FROM_ALL)
    nereus_patch_libjuice_turn_release("${nereus_libdatachannel_SOURCE_DIR}/deps/libjuice")
    nereus_patch_libdatachannel_remote_description_first("${nereus_libdatachannel_SOURCE_DIR}")

    # Older nested projects can still materialize these implementation
    # options in the parent cache. They are not NereusSDR user options.
    unset(PLOG_INSTALL CACHE)
    unset(JSON_Install CACHE)
endfunction()
