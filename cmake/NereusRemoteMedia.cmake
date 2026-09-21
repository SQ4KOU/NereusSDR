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
# =================================================================

include(FetchContent)

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

    # Older nested projects can still materialize these implementation
    # options in the parent cache. They are not NereusSDR user options.
    unset(PLOG_INSTALL CACHE)
    unset(JSON_Install CACHE)
endfunction()
