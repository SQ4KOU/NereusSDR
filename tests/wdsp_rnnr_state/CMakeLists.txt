cmake_minimum_required(VERSION 3.20)

# Standalone usage needs only already available dependency headers. It never
# fetches or links RNNoise, libspecbleach, FFTW, WDSP or the application.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_LIST_DIR)
    project(NereusRnnrStateRegression LANGUAGES C CXX)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Core)
    set(NEREUS_RNNR_HEADER_BUILD "" CACHE PATH "Existing app build with fetched dependency headers")
    set(NEREUS_RNNR_RNNOISE_INCLUDE "${NEREUS_RNNR_HEADER_BUILD}/_deps/rnnoise_upstream-src/include" CACHE PATH "Pinned RNNoise headers")
    set(NEREUS_RNNR_SPECBLEACH_INCLUDE "${NEREUS_RNNR_HEADER_BUILD}/_deps/libspecbleach_upstream-src/include" CACHE PATH "Pinned libspecbleach headers")
    set(NEREUS_RNNR_COMPAT_INCLUDE "${NEREUS_RNNR_HEADER_BUILD}/third_party/wdsp/specbleach_compat" CACHE PATH "Generated WDSP compatibility header directory")
    find_path(NEREUS_RNNR_FFTW_INCLUDE fftw3.h REQUIRED)
else()
    include(FetchContent)
    FetchContent_GetProperties(rnnoise_upstream SOURCE_DIR _rnnr_rnnoise_source)
    FetchContent_GetProperties(libspecbleach_upstream SOURCE_DIR _rnnr_specbleach_source)
    set(NEREUS_RNNR_RNNOISE_INCLUDE "${_rnnr_rnnoise_source}/include")
    set(NEREUS_RNNR_SPECBLEACH_INCLUDE "${_rnnr_specbleach_source}/include")
    set(NEREUS_RNNR_COMPAT_INCLUDE "${CMAKE_BINARY_DIR}/third_party/wdsp/specbleach_compat")
    set(NEREUS_RNNR_FFTW_INCLUDE "${FFTW3_INCLUDE_DIRS}")
endif()

get_filename_component(_rnnr_repo "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)

function(nereus_configure_rnnr_state_test target)
    find_package(Threads REQUIRED)
    foreach(_header IN ITEMS
            "${NEREUS_RNNR_RNNOISE_INCLUDE}/rnnoise.h"
            "${NEREUS_RNNR_SPECBLEACH_INCLUDE}/specbleach_adenoiser.h"
            "${NEREUS_RNNR_COMPAT_INCLUDE}/specbleach_compat.h")
        if(NOT EXISTS "${_header}")
            message(FATAL_ERROR "NR3 regression requires existing dependency header: ${_header}")
        endif()
    endforeach()
    target_include_directories(${target} PRIVATE
        "${_rnnr_repo}/third_party/wdsp/src"
        "${NEREUS_RNNR_RNNOISE_INCLUDE}"
        "${NEREUS_RNNR_SPECBLEACH_INCLUDE}"
        "${NEREUS_RNNR_FFTW_INCLUDE}")
    target_compile_definitions(${target} PRIVATE WDSP_STATIC_BUILD NOMINMAX)
    target_compile_features(${target} PRIVATE c_std_11 cxx_std_20)
    target_compile_options(${target} PRIVATE
        "$<$<COMPILE_LANG_AND_ID:C,GNU,Clang,AppleClang>:-include${NEREUS_RNNR_COMPAT_INCLUDE}/specbleach_compat.h>"
        "$<$<COMPILE_LANG_AND_ID:CXX,GNU,Clang,AppleClang>:-include${NEREUS_RNNR_COMPAT_INCLUDE}/specbleach_compat.h>"
        "$<$<COMPILE_LANG_AND_ID:C,MSVC>:/FI${NEREUS_RNNR_COMPAT_INCLUDE}/specbleach_compat.h>"
        "$<$<COMPILE_LANG_AND_ID:CXX,MSVC>:/FI${NEREUS_RNNR_COMPAT_INCLUDE}/specbleach_compat.h>")
    target_link_libraries(${target} PRIVATE Qt6::Core Threads::Threads)
endfunction()

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_LIST_DIR)
    add_executable(tst_wdsp_rnnr_state EXCLUDE_FROM_ALL
        "${_rnnr_repo}/tests/tst_wdsp_rnnr_state.cpp"
        "${_rnnr_repo}/third_party/wdsp/src/rnnr.c")
    nereus_configure_rnnr_state_test(tst_wdsp_rnnr_state)
    add_test(NAME tst_wdsp_rnnr_state COMMAND tst_wdsp_rnnr_state)
    set_tests_properties(tst_wdsp_rnnr_state PROPERTIES LABELS core TIMEOUT 120)
endif()
