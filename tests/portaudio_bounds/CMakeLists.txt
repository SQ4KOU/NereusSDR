# Device-free checks of the setup bounds used by our pinned PortAudio patch.
# Also configurable on its own; no application, driver or PortAudio runtime.
cmake_minimum_required(VERSION 3.20)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(NereusPortAudioBoundsChecks LANGUAGES C)
    enable_testing()
endif()

add_executable(tst_portaudio_bounds EXCLUDE_FROM_ALL tst_portaudio_bounds.c)
set_target_properties(tst_portaudio_bounds PROPERTIES
    AUTOMOC OFF AUTOUIC OFF AUTORCC OFF C_STANDARD 99 C_STANDARD_REQUIRED ON)
add_test(NAME tst_portaudio_bounds COMMAND tst_portaudio_bounds)
set_tests_properties(tst_portaudio_bounds PROPERTIES LABELS core TIMEOUT 30)
set_property(GLOBAL APPEND PROPERTY NEREUS_ALL_TESTS tst_portaudio_bounds)
set_property(GLOBAL APPEND PROPERTY NEREUS_TEST_LABEL_NAMES core)
set_property(GLOBAL APPEND PROPERTY NEREUS_TESTS_LABEL_core tst_portaudio_bounds)

if(NOT PORTAUDIO_SOURCE_DIR AND portaudio_SOURCE_DIR)
    set(PORTAUDIO_SOURCE_DIR "${portaudio_SOURCE_DIR}")
endif()
if(PORTAUDIO_SOURCE_DIR)
    include("${CMAKE_CURRENT_LIST_DIR}/../../cmake/NereusPortAudio.cmake")
    if(_nereus_portaudio_source)
        set(_checked_portaudio "${_nereus_portaudio_source}")
    else()
        nereus_stage_portaudio("${PORTAUDIO_SOURCE_DIR}" _checked_portaudio)
    endif()
    if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
        # Same add_subdirectory wiring as the application, compiled only;
        # nothing initializes or enumerates host APIs.
        set(CMAKE_POLICY_VERSION_MINIMUM 3.5)
        set(PA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(PA_BUILD_STATIC ON CACHE BOOL "" FORCE)
        add_subdirectory("${_checked_portaudio}" "${CMAKE_CURRENT_BINARY_DIR}/portaudio" EXCLUDE_FROM_ALL)
    endif()
    # Compile the real corrected adapter, but link no runtime or host API.
    add_library(portaudio_checked_buffer_processor OBJECT EXCLUDE_FROM_ALL
        "${_checked_portaudio}/src/common/pa_process.c")
    target_include_directories(portaudio_checked_buffer_processor PRIVATE
        "${_checked_portaudio}/include" "${_checked_portaudio}/src/common")
    set_target_properties(portaudio_checked_buffer_processor PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF C_STANDARD 99 C_STANDARD_REQUIRED ON)
    add_dependencies(tst_portaudio_bounds portaudio_checked_buffer_processor)
    add_executable(tst_portaudio_processor_setup EXCLUDE_FROM_ALL
        tst_portaudio_processor_setup.c)
    target_include_directories(tst_portaudio_processor_setup PRIVATE
        "${_checked_portaudio}/include" "${_checked_portaudio}/src/common")
    target_link_libraries(tst_portaudio_processor_setup PRIVATE portaudio_static)
    set_target_properties(tst_portaudio_processor_setup PROPERTIES
        AUTOMOC OFF AUTOUIC OFF AUTORCC OFF C_STANDARD 99 C_STANDARD_REQUIRED ON)
    add_test(NAME tst_portaudio_processor_setup COMMAND tst_portaudio_processor_setup)
    set_tests_properties(tst_portaudio_processor_setup PROPERTIES LABELS core TIMEOUT 30)
    set_property(GLOBAL APPEND PROPERTY NEREUS_ALL_TESTS tst_portaudio_processor_setup)
    set_property(GLOBAL APPEND PROPERTY NEREUS_TESTS_LABEL_core tst_portaudio_processor_setup)
    add_test(NAME portaudio_pinned_staging
        COMMAND "${CMAKE_COMMAND}"
            "-DROOT=${CMAKE_CURRENT_LIST_DIR}/../.."
            "-DSOURCE=${PORTAUDIO_SOURCE_DIR}"
            "-DWORK=${CMAKE_CURRENT_BINARY_DIR}/staging-check"
            -P "${CMAKE_CURRENT_LIST_DIR}/../scripts/check-portaudio-staging.cmake")
    set_tests_properties(portaudio_pinned_staging PROPERTIES LABELS core TIMEOUT 30)
endif()
