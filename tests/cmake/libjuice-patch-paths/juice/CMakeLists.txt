# no-port-check: NereusSDR-original. See ../CMakeLists.txt.
add_library(juice STATIC src/agent.c)
add_library(juice-static STATIC ${CMAKE_CURRENT_SOURCE_DIR}/src/agent.c)
