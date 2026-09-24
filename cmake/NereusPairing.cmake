# =================================================================
# cmake/NereusPairing.cmake  (NereusSDR)
# =================================================================
#
# no-port-check: NereusSDR-original. iPhone app plan Task 14 (R-IOS-08,
# D37).
#
# The pairing code's key exchange: SPAKE2+EE (jedisct1/spake2-ee,
# BSD-2-Clause) on libsodium (ISC). Both are fetched as archives pinned by
# SHA-256, the same way cmake/NereusRemoteMedia.cmake fetches the media
# stack, and built here as two static libraries linked into NereusCore.
# The iPhone and iPad app (plan Task 15) vendors the same two pins.
#
#   libsodium 1.0.22: the fixed release archive of the GitHub release for
#     tag 1.0.22-RELEASE. Never a "-stable" tarball: those are rolling
#     snapshots, so a pinned hash would break the next time one is cut.
#   spake2-ee fd3ea61f27a75ff63b0f192c9e619b5a494d048e: the repository has
#     no tags, so the commit's codeload archive is pinned.
#
# libsodium has no CMake build of its own. Its sources are compiled here
# from the pinned archive with the portable configuration: the reference C
# implementations on every platform, no hand-written assembly and no SIMD
# variants (those files compile to nothing unless their HAVE_*INTRIN_H
# macro is defined). That builds unchanged with Clang (macOS arm64 and
# x86_64), GCC (Linux x86_64 and arm64) and MSVC, which is what the pairing
# exchange needs: it runs once per pairing, not per sample. The macro set
# follows the build.zig libsodium ships in the same archive, minus its
# assembly and intrinsics entries. version.h is taken from the archive's
# builds/msvc/version.h, which the same build.zig uses in place of
# configure's version.h.in.
#
# docs/attribution/LIBSODIUM-PROVENANCE.md and SPAKE2EE-PROVENANCE.md
# record the pins, the archive hashes and the licences; the licence texts
# are packaging/third-party-licenses/libsodium.txt and spake2-ee.txt.
#
# =================================================================

include(FetchContent)

function(nereus_add_pairing_dependency)
    if(TARGET nereus_spake2ee)
        return()
    endif()

    FetchContent_Declare(nereus_libsodium
        URL https://github.com/jedisct1/libsodium/releases/download/1.0.22-RELEASE/libsodium-1.0.22.tar.gz
        URL_HASH SHA256=adbdd8f16149e81ac6078a03aca6fc03b592b89ef7b5ed83841c086191be3349
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR __nereus_no_add_subdirectory)
    FetchContent_Declare(nereus_spake2ee
        URL https://codeload.github.com/jedisct1/spake2-ee/tar.gz/fd3ea61f27a75ff63b0f192c9e619b5a494d048e
        URL_HASH SHA256=20d63587c1191b952e98b9a4d8bd557c8a6c4f6bfbac0b9fb77b28854c244bb6
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        SOURCE_SUBDIR __nereus_no_add_subdirectory)
    FetchContent_MakeAvailable(nereus_libsodium nereus_spake2ee)

    # ── libsodium ────────────────────────────────────────────────────────
    set(_sodium_root "${nereus_libsodium_SOURCE_DIR}/src/libsodium")
    file(GLOB_RECURSE _sodium_sources CONFIGURE_DEPENDS "${_sodium_root}/*.c")
    if(NOT _sodium_sources)
        message(FATAL_ERROR "NereusPairing: no libsodium sources under ${_sodium_root}")
    endif()

    # The headers, with version.h made as the archive's own build.zig makes
    # it (the prebuilt copy under builds/msvc), in one directory of the
    # build tree, so the fetched sources stay as the archive has them and
    # sodium/version.h finds its neighbours.
    set(_sodium_generated "${CMAKE_BINARY_DIR}/_deps/nereus_libsodium-include")
    file(COPY "${_sodium_root}/include/" DESTINATION "${_sodium_generated}")
    configure_file("${nereus_libsodium_SOURCE_DIR}/builds/msvc/version.h"
                   "${_sodium_generated}/sodium/version.h" COPYONLY)

    add_library(nereus_sodium STATIC ${_sodium_sources})
    set_target_properties(nereus_sodium PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_STANDARD 99
        C_EXTENSIONS ON
        C_VISIBILITY_PRESET hidden)
    target_include_directories(nereus_sodium
        PUBLIC "${_sodium_generated}"
        PRIVATE "${_sodium_generated}/sodium")
    # SODIUM_STATIC reaches every consumer: it is what makes the headers
    # declare plain (not dllimport) symbols on Windows.
    target_compile_definitions(nereus_sodium
        PUBLIC SODIUM_STATIC=1
        PRIVATE CONFIGURED=1 HAVE_INTTYPES_H=1 HAVE_STDINT_H=1)
    # Every platform this builds for is little-endian.
    if(CMAKE_C_BYTE_ORDER STREQUAL "BIG_ENDIAN")
        target_compile_definitions(nereus_sodium PRIVATE NATIVE_BIG_ENDIAN=1)
    else()
        target_compile_definitions(nereus_sodium PRIVATE NATIVE_LITTLE_ENDIAN=1)
    endif()
    # 128-bit integer arithmetic (the 64-bit field code) where the compiler
    # has it: GCC and Clang on a 64-bit target. MSVC has no __int128.
    if(NOT MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 8)
        target_compile_definitions(nereus_sodium PRIVATE HAVE_TI_MODE=1)
    endif()
    if(WIN32)
        target_compile_definitions(nereus_sodium PRIVATE
            HAVE_RAISE=1 _CRT_SECURE_NO_WARNINGS=1)
    elseif(APPLE)
        target_compile_definitions(nereus_sodium PRIVATE
            TLS=_Thread_local
            HAVE_ARC4RANDOM=1 HAVE_ARC4RANDOM_BUF=1 HAVE_CATCHABLE_ABRT=1
            HAVE_CATCHABLE_SEGV=1 HAVE_CLOCK_GETTIME=1 HAVE_GETENTROPY=1
            HAVE_GETPID=1 HAVE_MADVISE=1 HAVE_MEMSET_S=1 HAVE_MLOCK=1 HAVE_MMAP=1
            HAVE_MPROTECT=1 HAVE_NANOSLEEP=1 HAVE_POSIX_MEMALIGN=1 HAVE_PTHREAD=1
            HAVE_PTHREAD_PRIO_INHERIT=1 HAVE_RAISE=1 HAVE_SYSCONF=1
            HAVE_SYS_MMAN_H=1 HAVE_SYS_PARAM_H=1 HAVE_SYS_RANDOM_H=1
            HAVE_WEAK_SYMBOLS=1)
    else()
        target_compile_definitions(nereus_sodium PRIVATE
            _GNU_SOURCE=1 TLS=_Thread_local
            HAVE_CATCHABLE_ABRT=1 HAVE_CATCHABLE_SEGV=1 HAVE_CLOCK_GETTIME=1
            HAVE_GETPID=1 HAVE_MADVISE=1 HAVE_MLOCK=1 HAVE_MMAP=1 HAVE_MPROTECT=1
            HAVE_NANOSLEEP=1 HAVE_POSIX_MEMALIGN=1 HAVE_PTHREAD=1
            HAVE_PTHREAD_PRIO_INHERIT=1 HAVE_RAISE=1 HAVE_SYSCONF=1
            HAVE_SYS_MMAN_H=1 HAVE_SYS_PARAM_H=1 HAVE_SYS_RANDOM_H=1
            HAVE_WEAK_SYMBOLS=1)
    endif()
    if(MSVC)
        # An upstream library: its own warnings are not ours to fix.
        target_compile_options(nereus_sodium PRIVATE /W0)
    else()
        target_compile_options(nereus_sodium PRIVATE
            -w -fno-strict-aliasing -fno-strict-overflow -fwrapv)
    endif()
    if(NOT WIN32 AND NOT APPLE)
        find_package(Threads REQUIRED)
        target_link_libraries(nereus_sodium PRIVATE Threads::Threads)
    endif()

    # ── spake2-ee ────────────────────────────────────────────────────────
    add_library(nereus_spake2ee STATIC
        "${nereus_spake2ee_SOURCE_DIR}/src/crypto_spake.c")
    set_target_properties(nereus_spake2ee PROPERTIES
        POSITION_INDEPENDENT_CODE ON
        C_STANDARD 99
        C_EXTENSIONS ON
        C_VISIBILITY_PRESET hidden)
    target_include_directories(nereus_spake2ee PUBLIC "${nereus_spake2ee_SOURCE_DIR}/src")
    target_link_libraries(nereus_spake2ee PUBLIC nereus_sodium)
    if(MSVC)
        target_compile_options(nereus_spake2ee PRIVATE /W0)
    else()
        target_compile_options(nereus_spake2ee PRIVATE -w)
    endif()
endfunction()
