cmake_minimum_required(VERSION 3.21)
project(XerahS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# D-LIN-001: Qt 6, dynamically linked.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets DBus Network Test)
# .xann gzip container (ES-013); zlib licence is GPL-compatible.
find_package(ZLIB REQUIRED)

set(XERAHS_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." CACHE PATH "BriarForge/XerahS repository root")

add_library(xerahs_core STATIC
  src/actions/PostCapturePipeline.cpp
  src/actions/PostCapturePipeline.h
  src/capture/RegionGeometry.cpp
  src/capture/RegionGeometry.h
  src/capture/RegionSession.cpp
  src/capture/RegionSession.h
  src/image-editor/AnnotationDocument.cpp
  src/image-editor/AnnotationDocument.h
  src/image-editor/AnnotationRenderer.cpp
  src/image-editor/AnnotationRenderer.h
  src/image-editor/CanvasOperations.cpp
  src/image-editor/CanvasOperations.h
  src/image-editor/EditorSession.cpp
  src/image-editor/EditorSession.h
  src/image-editor/EditorViewport.cpp
  src/image-editor/EditorViewport.h
  src/image-editor/SourceBinding.cpp
  src/image-editor/SourceBinding.h
  src/image-editor/ImageComparison.cpp
  src/image-editor/ImageComparison.h
  src/image-editor/NewImage.cpp
  src/image-editor/NewImage.h
  src/image-editor/RecentImages.cpp
  src/image-editor/RecentImages.h
  src/image-editor/EditorPreferences.cpp
  src/image-editor/EditorPreferences.h
  src/image-editor/EditorAppearance.cpp
  src/image-editor/EditorAppearance.h
  src/image-editor/EditorPreferenceStore.cpp
  src/image-editor/EditorPreferenceStore.h
  src/image-editor/EditorWindowPlacement.cpp
  src/image-editor/EditorWindowPlacement.h
  src/naming/FilenameGenerator.cpp
  src/naming/FilenameGenerator.h
)
target_include_directories(xerahs_core PUBLIC src)
target_link_libraries(xerahs_core PUBLIC Qt6::Core PRIVATE ZLIB::ZLIB)
target_compile_options(xerahs_core PRIVATE -Wall -Wextra -Wpedantic
  # ES-025: binary64 in the order written, no fused multiply-add.
  -ffp-contract=off)

# Native image codecs are an I/O boundary; all canvas product decisions stay
# in the Qt Core library. The adapter and app share the same PNG preparation.
add_library(xerahs_editor_codecs STATIC app/EditorCanvasOperations.cpp app/EditorCanvasOperations.h
                                      app/RasterSource.cpp app/RasterSource.h
                                      app/ComparisonImages.cpp app/ComparisonImages.h
                                      app/NewImageSource.cpp app/NewImageSource.h)
target_include_directories(xerahs_editor_codecs PUBLIC app)
target_link_libraries(xerahs_editor_codecs PUBLIC xerahs_core Qt6::Gui)
target_compile_options(xerahs_editor_codecs PRIVATE -Wall -Wextra -Wpedantic)

add_executable(xerahs-conformance-adapter conformance-adapter/main.cpp)
target_link_libraries(xerahs-conformance-adapter PRIVATE xerahs_core xerahs_editor_codecs)
target_compile_options(xerahs-conformance-adapter PRIVATE -Wall -Wextra -Wpedantic)

# The XerahS application (D-LIN-001: Qt Widgets). Product logic stays in
# xerahs_core; app/ only wires it to Qt and the desktop.
add_executable(xerahs
  app/CaptureActions.cpp
  app/CaptureActions.h
  app/CaptureController.cpp
  app/CaptureController.h
  app/EditorSource.cpp
  app/EditorSource.h
  app/EditorOpen.cpp
  app/EditorOpen.h
  app/RecentImageMenu.cpp
  app/RecentImageMenu.h
  app/NativeEditorPreferences.cpp
  app/NativeEditorPreferences.h
  app/DrawingDefaultsDialog.cpp
  app/DrawingDefaultsDialog.h
  app/NativeEditorWindowState.cpp
  app/NativeEditorWindowState.h
  app/NativeDesktopAppearance.cpp
  app/NativeDesktopAppearance.h
  app/NativeEditorAppearance.cpp
  app/NativeEditorAppearance.h
  app/AppearanceDialog.cpp
  app/AppearanceDialog.h
  app/EditorCanvas.cpp
  app/EditorCanvas.h
  app/EditorWindow.cpp
  app/EditorWindow.h
  app/CanvasRotationDialog.cpp
  app/CanvasRotationDialog.h
  app/ImageComparisonDialog.cpp
  app/ImageComparisonDialog.h
  app/NewImageDialog.cpp
  app/NewImageDialog.h
  app/PortalScreenshot.cpp
  app/PortalScreenshot.h
  app/RegionOverlay.cpp
  app/RegionOverlay.h
  app/main.cpp
)
set_target_properties(xerahs PROPERTIES AUTOMOC ON)
target_link_libraries(xerahs PRIVATE xerahs_core xerahs_editor_codecs Qt6::Gui Qt6::Widgets Qt6::DBus Qt6::Network)
target_compile_options(xerahs PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
add_executable(editor-preferences-test tests/EditorPreferencesTest.cpp)
set_target_properties(editor-preferences-test PROPERTIES AUTOMOC ON)
target_link_libraries(editor-preferences-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME editor-preferences COMMAND editor-preferences-test)

add_executable(editor-appearance-test tests/EditorAppearanceTest.cpp)
set_target_properties(editor-appearance-test PROPERTIES AUTOMOC ON)
target_link_libraries(editor-appearance-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME editor-appearance COMMAND editor-appearance-test)

add_executable(native-editor-appearance-test tests/NativeEditorAppearanceTest.cpp
               app/NativeDesktopAppearance.cpp app/NativeDesktopAppearance.h
               app/NativeEditorAppearance.cpp app/NativeEditorAppearance.h
               app/NativeEditorPreferences.cpp app/NativeEditorPreferences.h
               app/AppearanceDialog.cpp app/AppearanceDialog.h)
set_target_properties(native-editor-appearance-test PROPERTIES AUTOMOC ON)
target_link_libraries(native-editor-appearance-test PRIVATE xerahs_core Qt6::Widgets Qt6::DBus Qt6::Test)
target_include_directories(native-editor-appearance-test PRIVATE app)
add_test(NAME native-editor-appearance COMMAND native-editor-appearance-test)
set_tests_properties(native-editor-appearance PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)

# Desktop transport tests run on their own bus, isolated from live settings.
find_program(XERAHS_DBUS_RUN_SESSION dbus-run-session REQUIRED)
add_executable(native-desktop-appearance-test tests/NativeDesktopAppearanceTest.cpp
               app/NativeDesktopAppearance.cpp app/NativeDesktopAppearance.h
               app/NativeEditorAppearance.cpp app/NativeEditorAppearance.h
               app/NativeEditorPreferences.cpp app/NativeEditorPreferences.h)
set_target_properties(native-desktop-appearance-test PROPERTIES AUTOMOC ON)
target_link_libraries(native-desktop-appearance-test PRIVATE xerahs_core Qt6::Widgets Qt6::DBus Qt6::Test)
target_include_directories(native-desktop-appearance-test PRIVATE app)
add_test(NAME native-desktop-appearance COMMAND "${XERAHS_DBUS_RUN_SESSION}"
         "--config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf" -- $<TARGET_FILE:native-desktop-appearance-test>)
set_tests_properties(native-desktop-appearance PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)

add_executable(editor-window-placement-test tests/EditorWindowPlacementTest.cpp)
set_target_properties(editor-window-placement-test PROPERTIES AUTOMOC ON)
target_link_libraries(editor-window-placement-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME editor-window-placement COMMAND editor-window-placement-test)

add_executable(native-editor-preferences-test tests/NativeEditorPreferencesTest.cpp
               app/NativeEditorPreferences.cpp app/NativeEditorPreferences.h
               app/DrawingDefaultsDialog.cpp app/DrawingDefaultsDialog.h
               app/RecentImageMenu.cpp app/RecentImageMenu.h)
set_target_properties(native-editor-preferences-test PROPERTIES AUTOMOC ON)
target_link_libraries(native-editor-preferences-test PRIVATE xerahs_core Qt6::Widgets Qt6::Test)
target_include_directories(native-editor-preferences-test PRIVATE app)
add_test(NAME native-editor-preferences COMMAND native-editor-preferences-test)
set_tests_properties(native-editor-preferences PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)

add_executable(native-editor-window-state-test tests/NativeEditorWindowStateTest.cpp
               app/NativeEditorWindowState.cpp app/NativeEditorWindowState.h
               app/NativeEditorPreferences.cpp app/NativeEditorPreferences.h)
set_target_properties(native-editor-window-state-test PROPERTIES AUTOMOC ON)
target_link_libraries(native-editor-window-state-test PRIVATE xerahs_core Qt6::Widgets Qt6::Test)
target_include_directories(native-editor-window-state-test PRIVATE app)
add_test(NAME native-editor-window-state COMMAND native-editor-window-state-test)
set_tests_properties(native-editor-window-state PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)

add_executable(recent-images-test tests/RecentImagesTest.cpp)
set_target_properties(recent-images-test PROPERTIES AUTOMOC ON)
target_link_libraries(recent-images-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME recent-images COMMAND recent-images-test)
add_executable(filename-generator-test tests/FilenameGeneratorTest.cpp)
set_target_properties(filename-generator-test PROPERTIES AUTOMOC ON)
target_link_libraries(filename-generator-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME filename-generator COMMAND filename-generator-test)

add_executable(post-capture-pipeline-test tests/PostCapturePipelineTest.cpp)
set_target_properties(post-capture-pipeline-test PROPERTIES AUTOMOC ON)
target_link_libraries(post-capture-pipeline-test PRIVATE xerahs_core Qt6::Test)
target_compile_definitions(post-capture-pipeline-test PRIVATE
  XERAHS_ACTIONS_CATALOGUE="${XERAHS_REPO_ROOT}/product-contract/capabilities/POST-CAPTURE-ACTIONS-001/actions.json")
add_test(NAME post-capture-pipeline COMMAND post-capture-pipeline-test)

add_executable(region-capture-test tests/RegionCaptureTest.cpp)
set_target_properties(region-capture-test PROPERTIES AUTOMOC ON)
target_link_libraries(region-capture-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME region-capture COMMAND region-capture-test)

add_executable(editor-session-test tests/EditorSessionTest.cpp)
set_target_properties(editor-session-test PROPERTIES AUTOMOC ON)
target_link_libraries(editor-session-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME editor-session COMMAND editor-session-test)

add_executable(editor-source-test tests/EditorSourceTest.cpp app/EditorSource.cpp)
set_target_properties(editor-source-test PROPERTIES AUTOMOC ON)
target_include_directories(editor-source-test PRIVATE app)
target_link_libraries(editor-source-test PRIVATE xerahs_editor_codecs Qt6::Test)
add_test(NAME editor-source COMMAND editor-source-test)
set_tests_properties(editor-source PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

add_executable(editor-viewport-test tests/EditorViewportTest.cpp)
set_target_properties(editor-viewport-test PROPERTIES AUTOMOC ON)
target_link_libraries(editor-viewport-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME editor-viewport COMMAND editor-viewport-test)

add_executable(editor-canvas-test tests/EditorCanvasTest.cpp app/EditorCanvas.cpp app/EditorCanvas.h)
set_target_properties(editor-canvas-test PROPERTIES AUTOMOC ON)
target_include_directories(editor-canvas-test PRIVATE app)
target_link_libraries(editor-canvas-test PRIVATE xerahs_core Qt6::Widgets Qt6::Test)
add_test(NAME editor-canvas COMMAND editor-canvas-test)
set_tests_properties(editor-canvas PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

add_executable(canvas-operations-test tests/CanvasOperationsTest.cpp)
set_target_properties(canvas-operations-test PROPERTIES AUTOMOC ON)
target_link_libraries(canvas-operations-test PRIVATE xerahs_editor_codecs Qt6::Test)
add_test(NAME canvas-operations COMMAND canvas-operations-test)

add_executable(image-comparison-test tests/ImageComparisonTest.cpp)
set_target_properties(image-comparison-test PROPERTIES AUTOMOC ON)
target_link_libraries(image-comparison-test PRIVATE xerahs_editor_codecs Qt6::Test)
add_test(NAME image-comparison COMMAND image-comparison-test)

add_executable(new-image-test tests/NewImageTest.cpp)
set_target_properties(new-image-test PROPERTIES AUTOMOC ON)
target_link_libraries(new-image-test PRIVATE xerahs_editor_codecs Qt6::Test)
add_test(NAME new-image COMMAND new-image-test)

add_executable(image-comparison-dialog-test tests/ImageComparisonDialogTest.cpp
               app/ImageComparisonDialog.cpp app/ImageComparisonDialog.h)
set_target_properties(image-comparison-dialog-test PROPERTIES AUTOMOC ON)
target_link_libraries(image-comparison-dialog-test PRIVATE xerahs_editor_codecs Qt6::Widgets Qt6::Test)
add_test(NAME image-comparison-dialog COMMAND image-comparison-dialog-test)
set_tests_properties(image-comparison-dialog PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)

add_executable(new-image-dialog-test tests/NewImageDialogTest.cpp app/NewImageDialog.cpp app/NewImageDialog.h)
set_target_properties(new-image-dialog-test PROPERTIES AUTOMOC ON)
target_link_libraries(new-image-dialog-test PRIVATE xerahs_editor_codecs Qt6::Widgets Qt6::Test)
add_test(NAME new-image-dialog COMMAND new-image-dialog-test)
set_tests_properties(new-image-dialog PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)

add_executable(editor-window-test tests/EditorWindowTest.cpp app/EditorWindow.cpp app/EditorWindow.h app/EditorOpen.cpp app/EditorOpen.h
               app/RecentImageMenu.cpp app/RecentImageMenu.h
               app/NativeEditorPreferences.cpp app/NativeEditorPreferences.h
               app/DrawingDefaultsDialog.cpp app/DrawingDefaultsDialog.h
               app/EditorCanvas.cpp app/EditorCanvas.h app/EditorSource.cpp
               app/NativeDesktopAppearance.cpp app/NativeDesktopAppearance.h
               app/NativeEditorAppearance.cpp app/NativeEditorAppearance.h
               app/AppearanceDialog.cpp app/AppearanceDialog.h
               app/NativeEditorWindowState.cpp app/NativeEditorWindowState.h
               app/CanvasRotationDialog.cpp app/CanvasRotationDialog.h
               app/ImageComparisonDialog.cpp app/ImageComparisonDialog.h
               app/NewImageDialog.cpp app/NewImageDialog.h)
set_target_properties(editor-window-test PROPERTIES AUTOMOC ON)
target_link_libraries(editor-window-test PRIVATE xerahs_editor_codecs Qt6::Widgets Qt6::DBus Qt6::Test)
add_test(NAME editor-window COMMAND editor-window-test)
set_tests_properties(editor-window PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)

find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
# Capabilities whose published vector operations this edition implements.
# Full requirement coverage remains in traceability, including partial rows.
# The full suite runs without --capability and still fails for missing seams.
set(XERAHS_IMPLEMENTED_CAPABILITIES FILENAME-GENERATION-001 POST-CAPTURE-ACTIONS-001 REGION-CAPTURE-001 EDITOR-SESSION-001 EDITOR-CANVAS-001 EDITOR-SETTINGS-001)
list(TRANSFORM XERAHS_IMPLEMENTED_CAPABILITIES PREPEND "--capability;" OUTPUT_VARIABLE XERAHS_CAPABILITY_ARGS)
add_test(NAME conformance
  COMMAND Python3::Interpreter "${XERAHS_REPO_ROOT}/conformance/runner/xerahs_conformance.py"
          --adapter $<TARGET_FILE:xerahs-conformance-adapter>
          --platform linux --edition linux-qt ${XERAHS_CAPABILITY_ARGS}
  WORKING_DIRECTORY "${XERAHS_REPO_ROOT}")
