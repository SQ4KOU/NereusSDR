cmake_minimum_required(VERSION 3.24)
project(LinuxPipeOwnershipFixture LANGUAGES CXX)

if(NOT UNIX)
    message(FATAL_ERROR "This fixture exercises real POSIX FIFOs and requires Unix")
endif()
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_CURRENT_BINARY_DIR)
    message(FATAL_ERROR "Use a separate, private build directory")
endif()

find_package(Qt6 REQUIRED COMPONENTS Core)
find_package(Python3 REQUIRED COMPONENTS Interpreter)
get_filename_component(NEREUS_SOURCE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../.." ABSOLUTE)
set(PRODUCTION_CPP "${NEREUS_SOURCE_ROOT}/src/core/audio/LinuxPipeBus.cpp")
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${PRODUCTION_CPP}")

# Every configure gets a private namespace. No production mode or path override
# is introduced. The generated source and its receipt are retained for review.
string(RANDOM LENGTH 20 ALPHABET 0123456789abcdef NAMESPACE_ID)
set(FIXTURE_ROOT "${CMAKE_CURRENT_BINARY_DIR}/owned-runtime-${NAMESPACE_ID}")
file(MAKE_DIRECTORY "${FIXTURE_ROOT}/fifo" "${CMAKE_CURRENT_BINARY_DIR}/staged")
file(CHMOD "${FIXTURE_ROOT}" "${FIXTURE_ROOT}/fifo"
     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)
set(FIXTURE_LIBRARY "${CMAKE_CURRENT_BINARY_DIR}/${CMAKE_SHARED_LIBRARY_PREFIX}fixture-pulse${CMAKE_SHARED_LIBRARY_SUFFIX}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/staged/receipt.txt" "namespace=${FIXTURE_ROOT}\n")
set(STAGED_SOURCES)
foreach(NAME IN ITEMS LinuxPipeBus LinuxPipeOwnership LinuxPipeControl)
    set(SOURCE "${NEREUS_SOURCE_ROOT}/src/core/audio/${NAME}.cpp")
    if(NOT EXISTS "${SOURCE}")
        continue()
    endif()
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${SOURCE}")
    file(READ "${SOURCE}" ORIGINAL_CPP)
    if(NAME STREQUAL "LinuxPipeOwnership")
        set(OWNERSHIP_ROOT "QStringLiteral(\"/tmp/nereussdr-vax-%1\").arg(::geteuid())")
        string(FIND "${ORIGINAL_CPP}" "${OWNERSHIP_ROOT}" OWNERSHIP_ROOT_OFFSET)
        if(OWNERSHIP_ROOT_OFFSET EQUAL -1)
            message(FATAL_ERROR "Refusing unstaged production ownership namespace; review root expression")
        endif()
        string(REPLACE "${OWNERSHIP_ROOT}"
            "qEnvironmentVariable(\"XDG_RUNTIME_DIR\") + QStringLiteral(\"/nereussdr-vax\")"
            ORIGINAL_CPP "${ORIGINAL_CPP}")
    endif()
    string(REPLACE "/tmp/nereussdr-vax-" "${FIXTURE_ROOT}/fifo/nereussdr-vax-"
           STAGED_CPP "${ORIGINAL_CPP}")
    string(REPLACE "/tmp/nereussdr-" "${FIXTURE_ROOT}/fallback-nereussdr-"
           STAGED_CPP "${STAGED_CPP}")
    if(APPLE)
        string(REPLACE "Q_OS_LINUX" "Q_OS_UNIX" STAGED_CPP "${STAGED_CPP}")
        string(REPLACE "/proc/" "${FIXTURE_ROOT}/proc/" STAGED_CPP "${STAGED_CPP}")
    endif()
    if(NAME STREQUAL "LinuxPipeControl")
        string(FIND "${STAGED_CPP}" "QStringLiteral(\"pulse\")" LIBRARY_LITERAL)
        if(LIBRARY_LITERAL EQUAL -1)
            message(FATAL_ERROR "Review changed production libpulse loading before staging")
        endif()
        string(REPLACE "QStringLiteral(\"pulse\")" "QStringLiteral(\"${FIXTURE_LIBRARY}\")"
               STAGED_CPP "${STAGED_CPP}")
    endif()
    set(STAGED "${CMAKE_CURRENT_BINARY_DIR}/staged/${NAME}.cpp")
    file(WRITE "${STAGED}" "${STAGED_CPP}")
    file(SHA256 "${SOURCE}" ORIGINAL_SHA256)
    file(SHA256 "${STAGED}" STAGED_SHA256)
    file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/staged/receipt.txt"
        "source=${SOURCE}\nsource_sha256=${ORIGINAL_SHA256}\nstaged_sha256=${STAGED_SHA256}\n")
    list(APPEND STAGED_SOURCES "${STAGED}")
endforeach()
file(APPEND "${CMAKE_CURRENT_BINARY_DIR}/staged/receipt.txt"
    "transform=ownership fixed per-UID root expression -> owned per-case XDG_RUNTIME_DIR/nereussdr-vax (required exact match); literal /tmp/nereussdr-vax- and /tmp/nereussdr- prefixes become private build paths; Apple only Q_OS_LINUX -> Q_OS_UNIX and /proc/ -> private proc tree; libpulse literal -> absolute fixture library\n")

configure_file(fixture_config.h.in fixture_config.h @ONLY)
configure_file(pactl.py.in pactl @ONLY)
file(CHMOD "${CMAKE_CURRENT_BINARY_DIR}/pactl"
     PERMISSIONS OWNER_READ OWNER_WRITE OWNER_EXECUTE)

add_executable(tst_linux_pipe_ownership
    fixture.cpp ${STAGED_SOURCES})
target_compile_features(tst_linux_pipe_ownership PRIVATE cxx_std_20)
target_include_directories(tst_linux_pipe_ownership PRIVATE
    "${NEREUS_SOURCE_ROOT}/src" "${NEREUS_SOURCE_ROOT}/src/core/audio"
    "${CMAKE_CURRENT_BINARY_DIR}")
target_link_libraries(tst_linux_pipe_ownership PRIVATE Qt6::Core)

if(EXISTS "${NEREUS_SOURCE_ROOT}/src/core/audio/PulseControlAbi.h")
    add_library(fixture-pulse SHARED fake_pulse.cpp)
    target_compile_features(fixture-pulse PRIVATE cxx_std_20)
    target_include_directories(fixture-pulse PRIVATE "${NEREUS_SOURCE_ROOT}/src" "${CMAKE_CURRENT_BINARY_DIR}")
    target_link_libraries(fixture-pulse PRIVATE Qt6::Core)
    add_dependencies(tst_linux_pipe_ownership fixture-pulse)
endif()

enable_testing()
foreach(CASE IN ITEMS baseline unrelated-live-rx same-role-object same-role-process
        unknown-same-role index-zero load-failure load-empty load-malformed
        unload-failure path-replacement crash-recover normal-reopen
        query-failure query-incomplete query-timeout epoch-restart all-roles replaced-on-load different-role-process
        pending-late-load crash-after-generation-removal missing-executable
        load-timeout-no-module unload-timeout)
    add_test(NAME "linux_pipe_ownership_${CASE}"
             COMMAND tst_linux_pipe_ownership "${CASE}")
    set_tests_properties("linux_pipe_ownership_${CASE}" PROPERTIES
        TIMEOUT 30 RUN_SERIAL TRUE LABELS core)
endforeach()
