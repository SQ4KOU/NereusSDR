# NereusSDR vendored patches in this file (vs. upstream radae_nopy
# at b289102, see third_party/rade/VERSION.txt):
#   1. BUILD_COMMAND $(MAKE) -> make
#      Make-only $-escape; Ninja can't parse it.
#   2. BUILD_BYPRODUCTS added to ExternalProject_Add(build_opus)
#      Required so Ninja knows libopus.a is produced by build_opus
#      and the consumer link step can wait on it.
#   3. PATCH_COMMAND ${CMAKE_SOURCE_DIR} -> ${CMAKE_CURRENT_LIST_DIR}/..
#      Upstream assumes standalone build (CMAKE_SOURCE_DIR == radae_nopy
#      root). When nested via add_subdirectory, CMAKE_SOURCE_DIR is the
#      parent project root; CMAKE_CURRENT_LIST_DIR works in both modes.
# All three are noted at their site as well. See Task A2 of
# docs/architecture/phase3j2-3r-spots-and-rade-design.md.

message(STATUS "Will build opus with FARGAN")

set(CONFIGURE_COMMAND ./autogen.sh && ./configure --with-pic --enable-osce --enable-dred --disable-shared --disable-doc --disable-extra-programs)

if (CMAKE_CROSSCOMPILING)
set(CONFIGURE_COMMAND ${CONFIGURE_COMMAND} --host=${CMAKE_C_COMPILER_TARGET} --target=${CMAKE_C_COMPILER_TARGET})
endif (CMAKE_CROSSCOMPILING)

if (NOT DEFINED OPUS_URL)
set(OPUS_URL https://github.com/xiph/opus/archive/940d4e5af64351ca8ba8390df3f555484c567fbb.zip)
endif (NOT DEFINED OPUS_URL)
message(STATUS "Using Opus from ${OPUS_URL}")

include(ExternalProject)
if(APPLE AND BUILD_OSX_UNIVERSAL)
# Opus ./configure doesn't behave properly when built as a universal binary;
# build it twice and use lipo to create a universal libopus.a instead.
ExternalProject_Add(build_opus_x86
    DOWNLOAD_EXTRACT_TIMESTAMP NO
    BUILD_IN_SOURCE 1
    PATCH_COMMAND sh -c "patch dnn/nnet.h < ${CMAKE_CURRENT_LIST_DIR}/../src/opus-nnet.h.diff"
    CONFIGURE_COMMAND ${CONFIGURE_COMMAND} --host=x86_64-apple-darwin --target=x86_64-apple-darwin CFLAGS=-arch\ x86_64\ -O2\ -mmacosx-version-min=10.11
    BUILD_COMMAND make
    # NereusSDR vendored patch: upstream uses $(MAKE), which Ninja
    # parses as a literal-dollar escape error ("bad $-escape"). $(MAKE)
    # is a Makefile-specific variable; replacing with plain `make`
    # works for both Ninja and Make top-level generators. See Task A2
    # of phase3j2-3r-spots-and-rade-design.md.
    INSTALL_COMMAND ""
    URL ${OPUS_URL}
)
ExternalProject_Add(build_opus_arm
    DOWNLOAD_EXTRACT_TIMESTAMP NO
    BUILD_IN_SOURCE 1
    PATCH_COMMAND sh -c "patch dnn/nnet.h < ${CMAKE_CURRENT_LIST_DIR}/../src/opus-nnet.h.diff"
    CONFIGURE_COMMAND ${CONFIGURE_COMMAND} --host=aarch64-apple-darwin --target=aarch64-apple-darwin CFLAGS=-arch\ arm64\ -O2\ -mmacosx-version-min=10.11
    BUILD_COMMAND make
    # NereusSDR vendored patch: upstream uses $(MAKE), which Ninja
    # parses as a literal-dollar escape error ("bad $-escape"). $(MAKE)
    # is a Makefile-specific variable; replacing with plain `make`
    # works for both Ninja and Make top-level generators. See Task A2
    # of phase3j2-3r-spots-and-rade-design.md.
    INSTALL_COMMAND ""
    URL ${OPUS_URL}
)

ExternalProject_Get_Property(build_opus_arm BINARY_DIR)
ExternalProject_Get_Property(build_opus_arm SOURCE_DIR)
set(OPUS_ARM_BINARY_DIR ${BINARY_DIR})
ExternalProject_Get_Property(build_opus_x86 BINARY_DIR)
set(OPUS_X86_BINARY_DIR ${BINARY_DIR})

add_custom_command(
    OUTPUT ${CMAKE_CURRENT_BINARY_DIR}/libopus${CMAKE_STATIC_LIBRARY_SUFFIX}
    COMMAND lipo ${OPUS_ARM_BINARY_DIR}/.libs/libopus${CMAKE_STATIC_LIBRARY_SUFFIX} ${OPUS_X86_BINARY_DIR}/.libs/libopus${CMAKE_STATIC_LIBRARY_SUFFIX} -output ${CMAKE_CURRENT_BINARY_DIR}/libopus${CMAKE_STATIC_LIBRARY_SUFFIX} -create
    DEPENDS build_opus_arm build_opus_x86)

add_custom_target(
    libopus.a
    DEPENDS ${CMAKE_CURRENT_BINARY_DIR}/libopus${CMAKE_STATIC_LIBRARY_SUFFIX})

include_directories(${SOURCE_DIR}/dnn ${SOURCE_DIR}/celt ${SOURCE_DIR}/include ${SOURCE_DIR})

add_library(opus STATIC IMPORTED)
add_dependencies(opus libopus.a)
set_target_properties(opus PROPERTIES
    IMPORTED_LOCATION "${CMAKE_CURRENT_BINARY_DIR}/libopus${CMAKE_STATIC_LIBRARY_SUFFIX}"
)

else(APPLE AND BUILD_OSX_UNIVERSAL)

# Disable Opus CPU feature detection when crosscompiling for ARM due to
# compiler issues building Windows for ARM version.
if (CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")
set(CONFIGURE_COMMAND ${CONFIGURE_COMMAND} --disable-rtcd)
endif (CMAKE_CROSSCOMPILING AND CMAKE_SYSTEM_PROCESSOR MATCHES "aarch64")

# NereusSDR vendored patch: pre-compute BINARY_DIR so we can declare
# BUILD_BYPRODUCTS. Ninja needs to know which file ExternalProject
# produces, otherwise it cannot wire the dependency from the consumer
# (librade.dylib) back to build_opus, and link fails with
# "missing and no known rule to make it". Make-generators don't need
# this. See Task A2 of phase3j2-3r-spots-and-rade-design.md.
set(_opus_binary_dir ${CMAKE_CURRENT_BINARY_DIR}/build_opus-prefix/src/build_opus)
set(_opus_static_lib ${_opus_binary_dir}/.libs/libopus${CMAKE_STATIC_LIBRARY_SUFFIX})

ExternalProject_Add(build_opus
    BUILD_IN_SOURCE 1
    PATCH_COMMAND sh -c "patch dnn/nnet.h < ${CMAKE_CURRENT_LIST_DIR}/../src/opus-nnet.h.diff"
    CONFIGURE_COMMAND ${CONFIGURE_COMMAND}
    BUILD_COMMAND make
    # NereusSDR vendored patch: upstream uses $(MAKE), which Ninja
    # parses as a literal-dollar escape error ("bad $-escape"). $(MAKE)
    # is a Makefile-specific variable; replacing with plain `make`
    # works for both Ninja and Make top-level generators. See Task A2
    # of phase3j2-3r-spots-and-rade-design.md.
    BUILD_BYPRODUCTS ${_opus_static_lib}
    INSTALL_COMMAND ""
    URL ${OPUS_URL}
)

ExternalProject_Get_Property(build_opus BINARY_DIR)
ExternalProject_Get_Property(build_opus SOURCE_DIR)
add_library(opus STATIC IMPORTED)
add_dependencies(opus build_opus)

set_target_properties(opus PROPERTIES
    IMPORTED_LOCATION "${BINARY_DIR}/.libs/libopus${CMAKE_STATIC_LIBRARY_SUFFIX}"
    IMPORTED_IMPLIB   "${BINARY_DIR}/.libs/libopus${CMAKE_STATIC_LIBRARY_SUFFIX}"
)

include_directories(${SOURCE_DIR}/dnn ${SOURCE_DIR}/celt ${SOURCE_DIR}/include ${SOURCE_DIR})
endif(APPLE AND BUILD_OSX_UNIVERSAL)
