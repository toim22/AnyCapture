# Integration tests (console): CV, .snap, rendering, PDF, GIF, effects, IPC, MP4 recording, cutting, OCR
set(SRC ${PROJECT_SOURCE_DIR}/src)
add_executable(SnapnabTests
    tests.cpp
    XmlReference.cpp
    ImageCodecReference.cpp
    SnapFileReference.cpp
    LibraryReference.cpp
    GifReference.cpp
    ${SRC}/capture/Recorder.cpp
    ${SRC}/capture/Panoramic.cpp
    ${SRC}/capture/Webcam.cpp
    ${SRC}/capture/Presets.cpp
    ${SRC}/editor/Video.cpp
    ${SRC}/editor/Document.cpp
    ${SRC}/editor/Render.cpp
    ${SRC}/editor/Serialize.cpp
    ${SRC}/editor/QuickStyles.cpp
    ${SRC}/editor/Pdf.cpp
    ${SRC}/editor/Clip.cpp
    ${SRC}/editor/Cursors.cpp
    ${SRC}/editor/EditorTools.cpp)
target_link_libraries(SnapnabTests PRIVATE SnapnabCommon mfplat mfreadwrite mfuuid mf winspool windowsapp)
# the snapcore C ABI (the tests of the parts moved to Rust call it directly)
target_include_directories(SnapnabTests PRIVATE ${PROJECT_SOURCE_DIR}/rust/snapcore/include)

add_test(NAME integration COMMAND SnapnabTests)
set_tests_properties(integration PROPERTIES TIMEOUT 600)
