# Workspace visual preview: builds the real MainWindow on a test board, with
# no radio, and captures deterministic screenshots. Not a test; never run by
# ctest. Build it explicitly: cmake --build build --target nereus_visual_preview
# Drive it with tests/visual/capture.py (see tests/visual/README.md).
add_executable(nereus_visual_preview EXCLUDE_FROM_ALL
    VisualPreviewMain.cpp
    PreviewScenes.cpp
    ControlsSheet.cpp
    PreviewData.cpp
    ../fakes/RemoteWindowHarness.cpp
    $<TARGET_OBJECTS:nereus_test_sandbox>)
target_include_directories(nereus_visual_preview PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(nereus_visual_preview PRIVATE NereusSDRLib Qt6::Test)
target_include_directories(nereus_visual_preview PRIVATE ${CMAKE_BINARY_DIR}/generated)
target_compile_definitions(nereus_visual_preview PRIVATE NEREUSSDR_VERSION="${PROJECT_VERSION}")
add_dependencies(nereus_visual_preview nereus_build_tag)
