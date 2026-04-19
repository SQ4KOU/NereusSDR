cmake_minimum_required(VERSION 3.20)
project(NereusSDRVAX LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Fetch libASPL
include(FetchContent)
FetchContent_Declare(libASPL
    GIT_REPOSITORY https://github.com/gavv/libASPL.git
    GIT_TAG        v3.1.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(libASPL)

# Build the plugin as a loadable bundle (.driver)
add_library(NereusSDRVAX MODULE NereusSDRVAX.cpp)
target_link_libraries(NereusSDRVAX PRIVATE libASPL)

# NOTE: AetherSDR's hal-plugin/CMakeLists.txt added
# `target_include_directories(... PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)`
# so the plugin could pull in a shared shm-layout header from its main tree.
# NereusSDR keeps the plugin self-contained: VaxShmBlock is defined inline in
# NereusSDRVAX.cpp. Sub-Phase 5.3's CoreAudioHalBus must agree on the same
# struct layout but shares no header with the plugin.

set_target_properties(NereusSDRVAX PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION "driver"
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
    # Don't add lib prefix
    PREFIX ""
)

# Install to the HAL plug-in directory (requires sudo)
install(TARGETS NereusSDRVAX
    DESTINATION "/Library/Audio/Plug-Ins/HAL"
)
