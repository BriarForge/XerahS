cmake_minimum_required(VERSION 3.21)
project(XerahS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# D-LIN-001: Qt 6, dynamically linked.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Gui Widgets DBus Network Test PrintSupport)
# .xann gzip container (ES-013); zlib licence is GPL-compatible.
find_package(ZLIB REQUIRED)

set(XERAHS_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." CACHE PATH "BriarForge/XerahS repository root")

add_library(xerahs_core STATIC
  src/watch/WatchFolders.cpp
  src/watch/WatchFolders.h
  src/settings/HotkeyBindings.cpp
  src/settings/ApplicationSettings.cpp
  src/workflows/WorkflowExecution.cpp
  src/workflows/WorkflowExecution.h
  src/workflows/CaptureWorkflows.cpp
  src/workflows/CaptureWorkflows.h
  src/workflows/WorkflowShortcuts.cpp
  src/workflows/WorkflowShortcuts.h
  src/shell/ActivationDispatcher.cpp
  src/shell/ActivationDispatcher.h
  src/shell/ActivationProtocol.cpp
  src/shell/ActivationProtocol.h
  src/actions/PostCapturePipeline.cpp
  src/actions/PostCapturePipeline.h
  src/actions/ActionSelection.cpp
  src/actions/ActionSelection.h
  src/actions/SaveAsTarget.cpp
  src/actions/SaveAsTarget.h
  src/actions/PipelineResultJson.cpp
  src/actions/PipelineResultJson.h
  src/actions/PipelineRetry.cpp
  src/actions/PipelineRetry.h
  src/actions/PipelineJournal.cpp
  src/actions/PipelineJournal.h
  src/history/CaptureHistory.cpp
  src/history/CaptureHistory.h
  src/capture/CaptureSchedule.cpp
  src/capture/CaptureSchedule.h
  src/capture/RegionGeometry.cpp
  src/capture/RegionGeometry.h
  src/capture/WindowSnapTargets.cpp
  src/capture/WindowSnapTargets.h
  src/capture/RegionSession.cpp
  src/capture/RegionSession.h
  src/image-editor/AnnotationDocument.cpp
  src/image-editor/AnnotationDocument.h
  src/image-editor/AnnotationTransfer.cpp
  src/image-editor/AnnotationTransfer.h
  src/image-editor/AnnotationRenderer.cpp
  src/image-editor/AnnotationRenderer.h
  src/image-editor/ShapePainter.cpp
  src/image-editor/ShapePainter.h
  src/image-editor/CanvasOperations.cpp
  src/image-editor/CanvasOperations.h
  src/image-editor/CropRegion.cpp
  src/image-editor/CropRegion.h
  src/image-editor/EditorSession.cpp
  src/image-editor/EditorSession.h
  src/image-editor/RectangleGeometry.cpp
  src/image-editor/RectangleGeometry.h
  src/image-editor/PrintLayout.cpp
  src/image-editor/PrintLayout.h
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
  src/image-editor/ToolbarPreferences.cpp
  src/image-editor/ToolbarPreferences.h
  src/image-editor/EditorAppearance.cpp
  src/image-editor/EditorAppearance.h
  src/image-editor/EditorPreferenceStore.cpp
  src/image-editor/EditorPreferenceStore.h
  src/image-editor/EditorWindowPlacement.cpp
  src/image-editor/EditorWindowPlacement.h
  src/naming/FilenameGenerator.cpp
  src/naming/FilenameGenerator.h
  src/naming/CounterStore.cpp
  src/naming/CounterStore.h
  src/uploads/UploadDestination.cpp
  src/uploads/UploadDestination.h
  src/uploads/CredentialStore.cpp
  src/uploads/CredentialStore.h
  src/uploads/DestinationCatalog.cpp
  src/uploads/DestinationCatalog.h
  src/uploads/SxcuImport.cpp
  src/uploads/SxcuImport.h
  src/tools/ColorFormats.cpp
  src/tools/ColorFormats.h
  src/tools/FileHash.cpp
  src/tools/FileHash.h
  src/media/VideoConversion.cpp
  src/media/VideoConversion.h
)
target_include_directories(xerahs_core PUBLIC src)
set_target_properties(xerahs_core PROPERTIES AUTOMOC ON)
# Qt Gui (private) supplies QPainter for shape painting shared with the canvas.
target_link_libraries(xerahs_core PUBLIC Qt6::Core PRIVATE Qt6::Gui ZLIB::ZLIB)
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

# Optional compositor transport stays outside the Qt Core production library.
add_library(xerahs_window_discovery STATIC app/WindowDiscovery.cpp app/WindowDiscovery.h)
target_link_libraries(xerahs_window_discovery PUBLIC xerahs_core Qt6::Network)

# CORE-013: desktop Secret Service transport for upload credentials.
add_library(xerahs_secret_service STATIC app/SecretServiceCredentialStore.cpp app/SecretServiceCredentialStore.h)
set_target_properties(xerahs_secret_service PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_secret_service PUBLIC app)
target_link_libraries(xerahs_secret_service PUBLIC xerahs_core Qt6::DBus)
target_compile_options(xerahs_secret_service PRIVATE -Wall -Wextra -Wpedantic)

add_library(xerahs_native_instance STATIC app/NativeInstance.cpp app/NativeInstance.h)
set_target_properties(xerahs_native_instance PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_native_instance PUBLIC app)
target_link_libraries(xerahs_native_instance PUBLIC xerahs_core Qt6::Network)
target_compile_options(xerahs_native_instance PRIVATE -Wall -Wextra -Wpedantic)

add_library(xerahs_pipeline_journal STATIC app/NativePipelineJournal.cpp app/NativePipelineJournal.h
  app/NativePrivateRecords.cpp app/NativePrivateRecords.h
  app/NativeWorkflowExecutions.cpp app/NativeWorkflowExecutions.h)
target_include_directories(xerahs_pipeline_journal PUBLIC app)
target_link_libraries(xerahs_pipeline_journal PUBLIC xerahs_core)
target_compile_options(xerahs_pipeline_journal PRIVATE -Wall -Wextra -Wpedantic)
add_library(xerahs_file_output STATIC app/CaptureFileIdentity.cpp app/CaptureFileOutput.cpp app/CaptureFileOutput.h)
target_include_directories(xerahs_file_output PUBLIC app)
target_link_libraries(xerahs_file_output PUBLIC xerahs_core Qt6::Gui Qt6::DBus)
target_compile_options(xerahs_file_output PRIVATE -Wall -Wextra -Wpedantic)

add_library(xerahs_editor_source STATIC app/EditorSource.cpp app/EditorSource.h app/EditorHistory.cpp app/EditorHistory.h app/EditorPreparation.cpp app/EditorPreparation.h app/EditorCanvasWorker.cpp app/EditorCanvasWorker.h app/EditorSave.cpp app/EditorSave.h app/EditorSaveProgress.cpp app/EditorCopy.cpp app/EditorCopy.h app/EditorPrint.cpp app/EditorPrint.h app/EditorPrintDialog.cpp app/AnnotationClipboard.cpp app/AnnotationClipboard.h)
target_include_directories(xerahs_editor_source PUBLIC app)
target_link_libraries(xerahs_editor_source PUBLIC xerahs_editor_codecs Qt6::Widgets Qt6::PrintSupport)
target_compile_options(xerahs_editor_source PRIVATE -Wall -Wextra -Wpedantic)
add_library(xerahs_editor_canvas STATIC app/EditorCanvas.cpp app/EditorCanvas.h
  app/NativeAnnotationTools.cpp app/NativeAnnotationTools.h
  app/AnnotationObjectNavigator.cpp app/AnnotationObjectNavigator.h
  app/RectanglePropertiesDialog.cpp app/RectanglePropertiesDialog.h
  app/SelectionRotationDialog.cpp app/SelectionRotationDialog.h
  app/AnnotationColorDialog.cpp app/AnnotationColorDialog.h
  app/CanvasCropDialog.cpp app/CanvasCropDialog.h)
set_target_properties(xerahs_editor_canvas PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_editor_canvas PUBLIC app)
target_link_libraries(xerahs_editor_canvas PUBLIC xerahs_core xerahs_editor_codecs Qt6::Widgets)
target_compile_options(xerahs_editor_canvas PRIVATE -Wall -Wextra -Wpedantic)

add_library(xerahs_action_selection STATIC app/ActionSelectionDialog.cpp app/ActionSelectionDialog.h
  app/CaptureSaveAsDialog.cpp app/CaptureSaveAsDialog.h app/CaptureAnnotationDialog.cpp app/CaptureAnnotationDialog.h
  app/PinWindow.cpp app/PinWindow.h)
set_target_properties(xerahs_action_selection PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_action_selection PUBLIC app)
target_link_libraries(xerahs_action_selection PUBLIC xerahs_core xerahs_editor_source xerahs_editor_canvas Qt6::Widgets)
target_compile_options(xerahs_action_selection PRIVATE -Wall -Wextra -Wpedantic)

add_library(xerahs_workflow_discovery STATIC app/WorkflowDiscovery.cpp app/WorkflowDiscovery.h app/WorkflowStoragePath.h)
target_include_directories(xerahs_workflow_discovery PUBLIC app)
target_link_libraries(xerahs_workflow_discovery PUBLIC xerahs_core)
target_compile_options(xerahs_workflow_discovery PRIVATE -Wall -Wextra -Wpedantic)

add_library(xerahs_workflow_shortcuts STATIC app/PortalWorkflowShortcuts.cpp app/PortalWorkflowShortcuts.h
  app/WorkflowShortcutActions.cpp app/WorkflowShortcutActions.h)
set_target_properties(xerahs_workflow_shortcuts PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_workflow_shortcuts PUBLIC app)
target_link_libraries(xerahs_workflow_shortcuts PUBLIC xerahs_core Qt6::DBus Qt6::Widgets)
target_compile_options(xerahs_workflow_shortcuts PRIVATE -Wall -Wextra -Wpedantic)

# The XerahS application (D-LIN-001: Qt Widgets). Product logic stays in
# xerahs_core; app/ only wires it to Qt and the desktop.
add_executable(xerahs
  app/CaptureWorkflows.cpp
  app/CaptureWorkflows.h
  app/ApplicationWindow.cpp
  app/ApplicationWindow.h
  app/CommandRegistry.cpp
  app/CommandRegistry.h
  app/AppContext.h
  app/features/ApplicationFeature.cpp
  app/features/ApplicationFeature.h
  app/features/CaptureFeature.cpp
  app/features/CaptureFeature.h
  app/features/CommandWindowAttach.cpp
  app/features/CommandWindowAttach.h
  app/features/FilesFeature.cpp
  app/features/FilesFeature.h
  app/features/PreferencesFeature.cpp
  app/features/PreferencesFeature.h
  app/features/RecordingFeature.cpp
  app/features/RecordingFeature.h
  app/features/ScheduleFeature.cpp
  app/features/ScheduleFeature.h
  app/features/ToolsFeature.cpp
  app/features/ToolsFeature.h
  app/features/UploadFeature.cpp
  app/features/UploadFeature.h
  app/features/WorkflowFeature.cpp
  app/features/WorkflowFeature.h
  app/PipelineHistoryDialog.cpp
  app/PipelineHistoryDialog.h
  app/CaptureHistoryWindow.cpp
  app/CaptureHistoryWindow.h
  app/PipelineResultDialog.cpp
  app/PipelineResultDialog.h
  app/CaptureActions.cpp
  app/CaptureActions.h
  app/CaptureSave.cpp
  app/CaptureSave.h
  app/CaptureController.cpp
  app/CaptureController.h
  app/DelayedCapture.cpp
  app/DelayedCapture.h
  app/AutoCaptureDialog.cpp
  app/AutoCaptureDialog.h
  app/ScheduledCapture.cpp
  app/ScheduledCapture.h
  app/CaptureRecoveryActions.cpp
  app/CaptureRecoveryActions.h
  app/CapturePipeline.cpp
  app/CapturePipeline.h
  app/SnapshotComposition.cpp
  app/SnapshotComposition.h
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
  app/NativeEditorToolbar.cpp
  app/NativeEditorToolbar.h
  app/ToolbarDialog.cpp
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
  app/SettingsDialog.cpp
  app/SettingsDialog.h
  app/XdgAutostart.cpp
  app/main.cpp
)
set_target_properties(xerahs PROPERTIES AUTOMOC ON)
target_include_directories(xerahs PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/app)
target_link_libraries(xerahs PRIVATE xerahs_window_discovery xerahs_file_output xerahs_action_selection xerahs_pipeline_journal xerahs_native_instance xerahs_workflow_shortcuts xerahs_workflow_discovery xerahs_editor_codecs Qt6::Gui Qt6::Widgets Qt6::DBus)
target_compile_options(xerahs PRIVATE -Wall -Wextra -Wpedantic)

find_program(XERAHS_DBUS_RUN_SESSION dbus-run-session REQUIRED)
enable_testing()
add_executable(annotation-transfer-test tests/AnnotationTransferTest.cpp)
set_target_properties(annotation-transfer-test PROPERTIES AUTOMOC ON)
target_link_libraries(annotation-transfer-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME annotation-transfer COMMAND annotation-transfer-test)
find_program(XERAHS_PDFTOPPM pdftoppm REQUIRED)
add_executable(editor-print-test tests/EditorPrintTest.cpp)
set_target_properties(editor-print-test PROPERTIES AUTOMOC ON)
target_link_libraries(editor-print-test PRIVATE xerahs_editor_source Qt6::Test)
target_compile_definitions(editor-print-test PRIVATE XERAHS_PDFTOPPM="${XERAHS_PDFTOPPM}")
add_test(NAME editor-print COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:editor-print-test>)
set_tests_properties(editor-print PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 45)
add_executable(editor-copy-test tests/EditorCopyTest.cpp)
set_target_properties(editor-copy-test PROPERTIES AUTOMOC ON)
target_link_libraries(editor-copy-test PRIVATE xerahs_editor_source Qt6::Test)
add_test(NAME editor-copy COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:editor-copy-test>)
set_tests_properties(editor-copy PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
add_executable(editor-save-test tests/EditorSaveTest.cpp)
set_target_properties(editor-save-test PROPERTIES AUTOMOC ON)
target_link_libraries(editor-save-test PRIVATE xerahs_editor_source Qt6::Test)
add_test(NAME editor-save COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:editor-save-test>)
set_tests_properties(editor-save PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
add_executable(pipeline-retry-test tests/PipelineRetryTest.cpp)
set_target_properties(pipeline-retry-test PROPERTIES AUTOMOC ON)
target_link_libraries(pipeline-retry-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME pipeline-retry COMMAND pipeline-retry-test)
add_executable(pipeline-journal-fixture tests/PipelineJournalFixture.cpp app/CapturePipeline.cpp app/CapturePipeline.h
  app/CaptureActions.cpp app/CaptureActions.h app/CaptureSave.cpp app/CaptureSave.h)
set_target_properties(pipeline-journal-fixture PROPERTIES AUTOMOC ON)
target_link_libraries(pipeline-journal-fixture PRIVATE xerahs_file_output xerahs_action_selection xerahs_pipeline_journal Qt6::Gui)
add_executable(pipeline-journal-test tests/PipelineJournalTest.cpp app/CapturePipeline.cpp app/CapturePipeline.h
  app/CaptureActions.cpp app/CaptureActions.h app/CaptureSave.cpp app/CaptureSave.h)
set_target_properties(pipeline-journal-test PROPERTIES AUTOMOC ON)
target_link_libraries(pipeline-journal-test PRIVATE xerahs_file_output xerahs_action_selection xerahs_pipeline_journal Qt6::Gui Qt6::Test)
add_dependencies(pipeline-journal-test pipeline-journal-fixture)
add_test(NAME pipeline-journal COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:pipeline-journal-test>)
set_tests_properties(pipeline-journal PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
add_executable(capture-history-window-test tests/CaptureHistoryWindowTest.cpp app/CaptureHistoryWindow.cpp app/CaptureHistoryWindow.h
  app/PipelineResultDialog.cpp app/PipelineResultDialog.h)
set_target_properties(capture-history-window-test PROPERTIES AUTOMOC ON)
target_link_libraries(capture-history-window-test PRIVATE xerahs_pipeline_journal Qt6::Widgets Qt6::DBus Qt6::Test)
add_test(NAME capture-history-window COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:capture-history-window-test>)
set_tests_properties(capture-history-window PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
add_executable(pipeline-history-dialog-test tests/PipelineHistoryDialogTest.cpp app/PipelineHistoryDialog.cpp app/PipelineHistoryDialog.h
  app/PipelineResultDialog.cpp app/PipelineResultDialog.h)
set_target_properties(pipeline-history-dialog-test PROPERTIES AUTOMOC ON)
target_link_libraries(pipeline-history-dialog-test PRIVATE xerahs_pipeline_journal Qt6::Widgets Qt6::Test)
add_test(NAME pipeline-history-dialog COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:pipeline-history-dialog-test>)
set_tests_properties(pipeline-history-dialog PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
add_executable(capture-pipeline-test tests/CapturePipelineTest.cpp app/CapturePipeline.cpp app/CapturePipeline.h
  app/CaptureActions.cpp app/CaptureActions.h app/CaptureSave.cpp app/CaptureSave.h)
set_target_properties(capture-pipeline-test PROPERTIES AUTOMOC ON)
target_include_directories(capture-pipeline-test PRIVATE app)
target_link_libraries(capture-pipeline-test PRIVATE xerahs_file_output xerahs_action_selection xerahs_pipeline_journal Qt6::Gui Qt6::Test)
add_test(NAME capture-pipeline COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:capture-pipeline-test>)
set_tests_properties(capture-pipeline PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
add_executable(action-selection-test tests/ActionSelectionTest.cpp app/CapturePipeline.cpp app/CapturePipeline.h
  app/CaptureActions.cpp app/CaptureActions.h app/CaptureSave.cpp app/CaptureSave.h)
set_target_properties(action-selection-test PROPERTIES AUTOMOC ON)
target_link_libraries(action-selection-test PRIVATE xerahs_file_output xerahs_action_selection xerahs_pipeline_journal Qt6::Test)
add_test(NAME action-selection COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:action-selection-test>)
set_tests_properties(action-selection PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
add_executable(capture-file-output-test tests/CaptureFileOutputTest.cpp app/CapturePipeline.cpp app/CapturePipeline.h
  app/CaptureActions.cpp app/CaptureActions.h app/CaptureSave.cpp app/CaptureSave.h)
set_target_properties(capture-file-output-test PROPERTIES AUTOMOC ON)
target_link_libraries(capture-file-output-test PRIVATE xerahs_file_output xerahs_action_selection xerahs_pipeline_journal Qt6::Test)
add_test(NAME capture-file-output COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:capture-file-output-test>)
set_tests_properties(capture-file-output PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
add_executable(capture-save-as-test tests/CaptureSaveAsTest.cpp app/CapturePipeline.cpp app/CapturePipeline.h
  app/CaptureActions.cpp app/CaptureActions.h app/CaptureSave.cpp app/CaptureSave.h)
set_target_properties(capture-save-as-test PROPERTIES AUTOMOC ON)
target_link_libraries(capture-save-as-test PRIVATE xerahs_file_output xerahs_action_selection xerahs_pipeline_journal Qt6::Test)
add_test(NAME capture-save-as COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:capture-save-as-test>)
set_tests_properties(capture-save-as PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
add_executable(capture-annotation-test tests/CaptureAnnotationTest.cpp app/CapturePipeline.cpp app/CapturePipeline.h
  app/CaptureActions.cpp app/CaptureActions.h app/CaptureSave.cpp app/CaptureSave.h
  app/NativeEditorPreferences.cpp app/NativeEditorPreferences.h)
set_target_properties(capture-annotation-test PROPERTIES AUTOMOC ON)
target_link_libraries(capture-annotation-test PRIVATE xerahs_file_output xerahs_action_selection xerahs_pipeline_journal Qt6::Test)
add_test(NAME capture-annotation COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:capture-annotation-test>)
set_tests_properties(capture-annotation PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
add_executable(capture-history-test tests/CaptureHistoryTest.cpp app/CapturePipeline.cpp app/CapturePipeline.h
  app/CaptureActions.cpp app/CaptureActions.h app/CaptureSave.cpp app/CaptureSave.h
  app/NativeEditorPreferences.cpp app/NativeEditorPreferences.h)
set_target_properties(capture-history-test PROPERTIES AUTOMOC ON)
target_link_libraries(capture-history-test PRIVATE xerahs_file_output xerahs_action_selection xerahs_pipeline_journal Qt6::Test)
add_test(NAME capture-history COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:capture-history-test>)
set_tests_properties(capture-history PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
add_executable(capture-paired-save-test tests/CapturePairedSaveTest.cpp app/CapturePipeline.cpp app/CapturePipeline.h
  app/CaptureActions.cpp app/CaptureActions.h app/CaptureSave.cpp app/CaptureSave.h app/PipelineResultDialog.cpp)
set_target_properties(capture-paired-save-test PROPERTIES AUTOMOC ON)
target_link_libraries(capture-paired-save-test PRIVATE xerahs_file_output xerahs_action_selection xerahs_pipeline_journal Qt6::Test)
add_test(NAME capture-paired-save COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:capture-paired-save-test>)
set_tests_properties(capture-paired-save PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
add_executable(capture-save-test tests/CaptureSaveTest.cpp app/CaptureSave.cpp app/CaptureSave.h)
set_target_properties(capture-save-test PROPERTIES AUTOMOC ON)
target_include_directories(capture-save-test PRIVATE app)
target_link_libraries(capture-save-test PRIVATE xerahs_file_output xerahs_core Qt6::Gui Qt6::Test)
add_test(NAME capture-save COMMAND capture-save-test)
add_executable(snapshot-composition-test tests/SnapshotCompositionTest.cpp app/SnapshotComposition.cpp app/SnapshotComposition.h)
set_target_properties(snapshot-composition-test PROPERTIES AUTOMOC ON)
target_include_directories(snapshot-composition-test PRIVATE app)
target_link_libraries(snapshot-composition-test PRIVATE xerahs_core Qt6::Gui Qt6::Test)
add_test(NAME snapshot-composition COMMAND snapshot-composition-test)
add_executable(activation-protocol-test tests/ActivationProtocolTest.cpp)
set_target_properties(activation-protocol-test PROPERTIES AUTOMOC ON)
target_link_libraries(activation-protocol-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME activation-protocol COMMAND activation-protocol-test)
add_executable(application-window-test tests/ApplicationWindowTest.cpp app/ApplicationWindow.cpp app/ApplicationWindow.h
  app/CommandRegistry.cpp app/CommandRegistry.h app/features/ToolsFeature.cpp app/features/ToolsFeature.h)
set_target_properties(application-window-test PROPERTIES AUTOMOC ON)
target_include_directories(application-window-test PRIVATE app)
target_link_libraries(application-window-test PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME application-window COMMAND application-window-test)
set_tests_properties(application-window PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
add_executable(command-registry-test tests/CommandRegistryTest.cpp app/CommandRegistry.cpp app/CommandRegistry.h)
set_target_properties(command-registry-test PROPERTIES AUTOMOC ON)
target_include_directories(command-registry-test PRIVATE app)
target_link_libraries(command-registry-test PRIVATE Qt6::Widgets Qt6::Test)
add_test(NAME command-registry COMMAND command-registry-test)
set_tests_properties(command-registry PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
add_executable(native-instance-fixture tests/NativeInstanceFixture.cpp)
target_link_libraries(native-instance-fixture PRIVATE xerahs_native_instance)
add_executable(native-instance-test tests/NativeInstanceTest.cpp)
set_target_properties(native-instance-test PROPERTIES AUTOMOC ON)
target_link_libraries(native-instance-test PRIVATE xerahs_native_instance Qt6::Test)
add_dependencies(native-instance-test native-instance-fixture xerahs)
add_test(NAME native-instance COMMAND native-instance-test)
set_tests_properties(native-instance PROPERTIES TIMEOUT 45)
add_executable(toolbar-preferences-test tests/ToolbarPreferencesTest.cpp)
set_target_properties(toolbar-preferences-test PROPERTIES AUTOMOC ON)
target_link_libraries(toolbar-preferences-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME toolbar-preferences COMMAND toolbar-preferences-test)
add_executable(native-editor-toolbar-test tests/NativeEditorToolbarTest.cpp
               app/NativeEditorToolbar.cpp app/NativeEditorToolbar.h app/ToolbarDialog.cpp
               app/NativeEditorPreferences.cpp app/NativeEditorPreferences.h
               app/NativeEditorAppearance.cpp app/NativeEditorAppearance.h
               app/NativeDesktopAppearance.cpp app/NativeDesktopAppearance.h)
set_target_properties(native-editor-toolbar-test PROPERTIES AUTOMOC ON)
target_link_libraries(native-editor-toolbar-test PRIVATE xerahs_core Qt6::Widgets Qt6::DBus Qt6::Test)
target_include_directories(native-editor-toolbar-test PRIVATE app)
add_test(NAME native-editor-toolbar COMMAND native-editor-toolbar-test)
set_tests_properties(native-editor-toolbar PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
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
add_executable(portal-screenshot-test tests/PortalScreenshotTest.cpp app/PortalScreenshot.cpp app/PortalScreenshot.h
               app/CaptureController.cpp app/CaptureController.h app/CaptureActions.cpp app/CaptureActions.h
               app/CaptureSave.cpp app/CaptureSave.h
               app/CapturePipeline.cpp app/CapturePipeline.h
               app/CaptureRecoveryActions.cpp app/CaptureRecoveryActions.h
               app/PipelineResultDialog.cpp app/PipelineResultDialog.h
               app/ApplicationWindow.cpp app/ApplicationWindow.h
               app/CommandRegistry.cpp app/CommandRegistry.h
               app/SnapshotComposition.cpp app/SnapshotComposition.h
               app/RegionOverlay.cpp app/RegionOverlay.h)
set_target_properties(portal-screenshot-test PROPERTIES AUTOMOC ON)
target_link_libraries(portal-screenshot-test PRIVATE xerahs_window_discovery xerahs_file_output xerahs_action_selection xerahs_pipeline_journal xerahs_editor_codecs Qt6::Widgets Qt6::DBus Qt6::Test)
target_include_directories(portal-screenshot-test PRIVATE app)
add_dependencies(portal-screenshot-test xerahs)
add_test(NAME portal-screenshot COMMAND "${XERAHS_DBUS_RUN_SESSION}"
         "--config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf" -- $<TARGET_FILE:portal-screenshot-test>)
set_tests_properties(portal-screenshot PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 45)
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
               app/NativeDesktopAppearance.cpp app/NativeDesktopAppearance.h
               app/NativeEditorAppearance.cpp app/NativeEditorAppearance.h
               app/RecentImageMenu.cpp app/RecentImageMenu.h)
set_target_properties(native-editor-preferences-test PROPERTIES AUTOMOC ON)
target_link_libraries(native-editor-preferences-test PRIVATE xerahs_core Qt6::Widgets Qt6::DBus Qt6::Test)
target_include_directories(native-editor-preferences-test PRIVATE app)
add_test(NAME native-editor-preferences COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:native-editor-preferences-test>)
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
add_executable(counter-store-test tests/CounterStoreTest.cpp)
set_target_properties(counter-store-test PROPERTIES AUTOMOC ON)
target_link_libraries(counter-store-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME counter-store COMMAND counter-store-test)

add_executable(upload-destination-test tests/UploadDestinationTest.cpp)
set_target_properties(upload-destination-test PROPERTIES AUTOMOC ON)
target_link_libraries(upload-destination-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME upload-destination COMMAND upload-destination-test)

add_executable(sxcu-import-test tests/SxcuImportTest.cpp)
set_target_properties(sxcu-import-test PROPERTIES AUTOMOC ON)
target_link_libraries(sxcu-import-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME sxcu-import COMMAND sxcu-import-test)

add_executable(post-capture-pipeline-test tests/PostCapturePipelineTest.cpp)
set_target_properties(post-capture-pipeline-test PROPERTIES AUTOMOC ON)
target_link_libraries(post-capture-pipeline-test PRIVATE xerahs_core Qt6::Test)
target_compile_definitions(post-capture-pipeline-test PRIVATE
  XERAHS_ACTIONS_CATALOGUE="${XERAHS_REPO_ROOT}/product-contract/capabilities/POST-CAPTURE-ACTIONS-001/actions.json")
add_test(NAME post-capture-pipeline COMMAND post-capture-pipeline-test)

add_executable(capture-schedule-test tests/CaptureScheduleTest.cpp)
set_target_properties(capture-schedule-test PROPERTIES AUTOMOC ON)
target_link_libraries(capture-schedule-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME capture-schedule COMMAND capture-schedule-test)

add_executable(capture-schedule-dialog-test tests/CaptureScheduleDialogTest.cpp
               app/DelayedCapture.cpp app/DelayedCapture.h
               app/AutoCaptureDialog.cpp app/AutoCaptureDialog.h)
set_target_properties(capture-schedule-dialog-test PROPERTIES AUTOMOC ON)
target_include_directories(capture-schedule-dialog-test PRIVATE app)
target_link_libraries(capture-schedule-dialog-test PRIVATE xerahs_core Qt6::Widgets Qt6::Test)
add_test(NAME capture-schedule-dialog COMMAND capture-schedule-dialog-test)
set_tests_properties(capture-schedule-dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 30)

add_executable(region-capture-test tests/RegionCaptureTest.cpp)
set_target_properties(region-capture-test PROPERTIES AUTOMOC ON)
target_link_libraries(region-capture-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME region-capture COMMAND region-capture-test)

add_executable(window-snap-targets-test tests/WindowSnapTargetsTest.cpp)
set_target_properties(window-snap-targets-test PROPERTIES AUTOMOC ON)
target_link_libraries(window-snap-targets-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME window-snap-targets COMMAND window-snap-targets-test)

add_executable(native-window-discovery-test tests/NativeWindowDiscoveryTest.cpp)
set_target_properties(native-window-discovery-test PROPERTIES AUTOMOC ON)
target_include_directories(native-window-discovery-test PRIVATE app)
target_link_libraries(native-window-discovery-test PRIVATE xerahs_window_discovery Qt6::Test)
add_test(NAME native-window-discovery COMMAND native-window-discovery-test)
set_tests_properties(native-window-discovery PROPERTIES TIMEOUT 10)

add_executable(editor-session-test tests/EditorSessionTest.cpp)
set_target_properties(editor-session-test PROPERTIES AUTOMOC ON)
target_link_libraries(editor-session-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME editor-session COMMAND editor-session-test)

add_executable(editor-source-test tests/EditorSourceTest.cpp)
set_target_properties(editor-source-test PROPERTIES AUTOMOC ON)
target_include_directories(editor-source-test PRIVATE app)
target_link_libraries(editor-source-test PRIVATE xerahs_editor_source Qt6::Test)
add_test(NAME editor-source COMMAND editor-source-test)
set_tests_properties(editor-source PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)

add_executable(editor-viewport-test tests/EditorViewportTest.cpp)
set_target_properties(editor-viewport-test PROPERTIES AUTOMOC ON)
target_link_libraries(editor-viewport-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME editor-viewport COMMAND editor-viewport-test)

add_executable(editor-canvas-test tests/EditorCanvasTest.cpp)
set_target_properties(editor-canvas-test PROPERTIES AUTOMOC ON)
target_include_directories(editor-canvas-test PRIVATE app)
target_link_libraries(editor-canvas-test PRIVATE xerahs_editor_canvas Qt6::Widgets Qt6::Test)
add_test(NAME editor-canvas COMMAND editor-canvas-test)
add_executable(annotation-shapes-test tests/AnnotationShapesTest.cpp)
set_target_properties(annotation-shapes-test PROPERTIES AUTOMOC ON)
target_include_directories(annotation-shapes-test PRIVATE app)
target_link_libraries(annotation-shapes-test PRIVATE xerahs_editor_canvas Qt6::Widgets Qt6::Test)
add_test(NAME annotation-shapes COMMAND annotation-shapes-test)
set_tests_properties(annotation-shapes PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
set_tests_properties(editor-canvas PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen)
add_executable(annotation-object-navigator-test tests/AnnotationObjectNavigatorTest.cpp)
set_target_properties(annotation-object-navigator-test PROPERTIES AUTOMOC ON)
target_link_libraries(annotation-object-navigator-test PRIVATE xerahs_editor_canvas Qt6::Test)
add_test(NAME annotation-object-navigator COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:annotation-object-navigator-test>)
set_tests_properties(annotation-object-navigator PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)

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
               app/NativeEditorToolbar.cpp app/NativeEditorToolbar.h app/ToolbarDialog.cpp
               app/RecentImageMenu.cpp app/RecentImageMenu.h
               app/NativeEditorPreferences.cpp app/NativeEditorPreferences.h
               app/DrawingDefaultsDialog.cpp app/DrawingDefaultsDialog.h

               app/NativeDesktopAppearance.cpp app/NativeDesktopAppearance.h
               app/NativeEditorAppearance.cpp app/NativeEditorAppearance.h
               app/AppearanceDialog.cpp app/AppearanceDialog.h
               app/NativeEditorWindowState.cpp app/NativeEditorWindowState.h
               app/CanvasRotationDialog.cpp app/CanvasRotationDialog.h
               app/ImageComparisonDialog.cpp app/ImageComparisonDialog.h
               app/NewImageDialog.cpp app/NewImageDialog.h)
set_target_properties(editor-window-test PROPERTIES AUTOMOC ON)
target_link_libraries(editor-window-test PRIVATE xerahs_editor_source xerahs_editor_canvas xerahs_editor_codecs Qt6::Widgets Qt6::DBus Qt6::Test)
# The aggregate class has 323 behavior rows and exceeded its 30 s CI deadline.
# Partition actual Qt slots without omitting rows or raising any deadline.
foreach(partition RANGE 0 2)
  math(EXPR part_number "${partition} + 1")
  add_test(NAME editor-window-${part_number} COMMAND ${XERAHS_DBUS_RUN_SESSION}
    --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:editor-window-test> --partition=${partition}/3)
  set_tests_properties(editor-window-${part_number} PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
endforeach()

# Exercise the real configuration/editor surface without growing the existing
# editor suite's deadline. Reuse its production sources, not its test fixture.
get_target_property(XERAHS_PREFERENCE_TEST_SOURCES editor-window-test SOURCES)
list(REMOVE_ITEM XERAHS_PREFERENCE_TEST_SOURCES tests/EditorWindowTest.cpp)
add_executable(native-preference-dialogs-test tests/PreferenceDialogTest.cpp ${XERAHS_PREFERENCE_TEST_SOURCES})
set_target_properties(native-preference-dialogs-test PROPERTIES AUTOMOC ON)
target_link_libraries(native-preference-dialogs-test PRIVATE xerahs_editor_source xerahs_editor_canvas xerahs_editor_codecs Qt6::Widgets Qt6::DBus Qt6::Test)
add_test(NAME native-preference-dialogs COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:native-preference-dialogs-test>)
set_tests_properties(native-preference-dialogs PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)

add_executable(editor-open-test tests/EditorOpenTest.cpp app/EditorOpen.cpp app/EditorOpen.h)
set_target_properties(editor-open-test PROPERTIES AUTOMOC ON)
target_link_libraries(editor-open-test PRIVATE xerahs_editor_source Qt6::Widgets Qt6::Test)
add_test(NAME editor-open COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:editor-open-test>)
set_tests_properties(editor-open PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)

find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
add_executable(capture-workflows-test tests/CaptureWorkflowsTest.cpp)
set_target_properties(capture-workflows-test PROPERTIES AUTOMOC ON)
target_link_libraries(capture-workflows-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME capture-workflows COMMAND capture-workflows-test)
add_executable(native-capture-workflows-test tests/NativeCaptureWorkflowsTest.cpp
  app/CaptureWorkflows.cpp app/CaptureWorkflows.h app/CapturePipeline.cpp app/CapturePipeline.h
  app/CaptureActions.cpp app/CaptureActions.h app/CaptureSave.cpp app/CaptureSave.h
  app/NativeEditorPreferences.cpp app/NativeEditorPreferences.h
  app/NativeEditorAppearance.cpp app/NativeEditorAppearance.h
  app/NativeDesktopAppearance.cpp app/NativeDesktopAppearance.h)
set_target_properties(native-capture-workflows-test PROPERTIES AUTOMOC ON)
target_link_libraries(native-capture-workflows-test PRIVATE xerahs_file_output xerahs_action_selection xerahs_pipeline_journal Qt6::Widgets Qt6::DBus Qt6::Test)
add_test(NAME native-capture-workflows COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:native-capture-workflows-test>)
set_tests_properties(native-capture-workflows PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
add_executable(workflow-shortcuts-test tests/WorkflowShortcutsTest.cpp
  app/PortalScreenshot.cpp app/PortalScreenshot.h app/CaptureController.cpp app/CaptureController.h
  app/CaptureActions.cpp app/CaptureActions.h app/CaptureSave.cpp app/CaptureSave.h
  app/CapturePipeline.cpp app/CapturePipeline.h app/SnapshotComposition.cpp app/SnapshotComposition.h
  app/RegionOverlay.cpp app/RegionOverlay.h app/ApplicationWindow.cpp app/ApplicationWindow.h
  app/CommandRegistry.cpp app/CommandRegistry.h)
set_target_properties(workflow-shortcuts-test PROPERTIES AUTOMOC ON)
target_link_libraries(workflow-shortcuts-test PRIVATE xerahs_window_discovery xerahs_workflow_shortcuts xerahs_file_output xerahs_action_selection xerahs_pipeline_journal Qt6::Test)
add_test(NAME workflow-shortcuts COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:workflow-shortcuts-test>)
set_tests_properties(workflow-shortcuts PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 30)

# Capabilities with production implementation and requirement traceability.
# Full requirement coverage remains in traceability, including partial rows.
# The full suite runs without --capability and still fails for missing seams.
set(XERAHS_IMPLEMENTED_CAPABILITIES CORE-PLATFORM-001 FILENAME-GENERATION-001 POST-CAPTURE-ACTIONS-001 REGION-CAPTURE-001 EDITOR-SESSION-001 EDITOR-CANVAS-001 EDITOR-SETTINGS-001 WORKFLOW-CATALOG-001 SETTINGS-CATALOG-001)
set(XERAHS_CAPABILITY_ARGS)
foreach(capability IN LISTS XERAHS_IMPLEMENTED_CAPABILITIES)
  if(EXISTS "${XERAHS_REPO_ROOT}/product-contract/capabilities/${capability}/test-vectors.json")
    list(APPEND XERAHS_CAPABILITY_ARGS --capability "${capability}")
  elseif(capability STREQUAL "CORE-PLATFORM-001" OR capability STREQUAL "WORKFLOW-CATALOG-001" OR capability STREQUAL "SETTINGS-CATALOG-001")
    message(STATUS "${capability}: no published test-vectors.json; native tests are partial evidence. Shared conformance and qualification remain Required.")
  else()
    message(FATAL_ERROR "${capability}: required published test-vectors.json is missing.")
  endif()
endforeach()
add_test(NAME conformance
  COMMAND Python3::Interpreter "${XERAHS_REPO_ROOT}/conformance/runner/xerahs_conformance.py"
          --adapter $<TARGET_FILE:xerahs-conformance-adapter>
          --platform linux --edition linux-qt ${XERAHS_CAPABILITY_ARGS}
  WORKING_DIRECTORY "${XERAHS_REPO_ROOT}")

add_executable(workflow-execution-test tests/WorkflowExecutionTest.cpp)
set_target_properties(workflow-execution-test PROPERTIES AUTOMOC ON)
target_link_libraries(workflow-execution-test PRIVATE xerahs_pipeline_journal Qt6::Test)
add_dependencies(workflow-execution-test xerahs)
add_test(NAME workflow-execution COMMAND workflow-execution-test)
set_tests_properties(workflow-execution PROPERTIES TIMEOUT 30)

add_executable(application-settings-test tests/ApplicationSettingsTest.cpp
  app/XdgAutostart.cpp app/SettingsDialog.cpp)
set_target_properties(application-settings-test PROPERTIES AUTOMOC ON)
target_link_libraries(application-settings-test PRIVATE xerahs_workflow_shortcuts Qt6::Test)
add_test(NAME application-settings COMMAND application-settings-test)
set_tests_properties(application-settings PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 30)

# Optional dynamically linked native recording transport; product decisions use Qt Core only.
option(XERAHS_RECORDING "Enable GStreamer recording when available" ON)
find_package(PkgConfig QUIET)
if(XERAHS_RECORDING AND PkgConfig_FOUND)
  pkg_check_modules(GSTREAMER QUIET IMPORTED_TARGET gstreamer-1.0)
endif()
add_library(xerahs_recording_core STATIC src/recording/Recording.cpp src/recording/Recording.h)
set_target_properties(xerahs_recording_core PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_recording_core PUBLIC src)
target_link_libraries(xerahs_recording_core PUBLIC Qt6::Core PRIVATE xerahs_core)
target_compile_options(xerahs_recording_core PRIVATE -Wall -Wextra -Wpedantic)
add_library(xerahs_recording_devices STATIC app/RecordingDevices.cpp)
target_include_directories(xerahs_recording_devices PUBLIC app)
target_link_libraries(xerahs_recording_devices PUBLIC xerahs_recording_core)
if(XERAHS_RECORDING AND GSTREAMER_FOUND)
  target_link_libraries(xerahs_recording_devices PUBLIC PkgConfig::GSTREAMER)
  target_compile_definitions(xerahs_recording_devices PUBLIC XERAHS_GSTREAMER=1)
endif()
target_link_libraries(application-settings-test PRIVATE xerahs_recording_devices)
add_library(xerahs_recording STATIC app/RecordingController.cpp app/RecordingController.h app/RecordingActions.cpp app/RecordingActions.h)
set_target_properties(xerahs_recording PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_recording PUBLIC app)
target_link_libraries(xerahs_recording PUBLIC xerahs_recording_core xerahs_recording_devices Qt6::DBus Qt6::Widgets)
if(XERAHS_RECORDING AND GSTREAMER_FOUND)
  target_link_libraries(xerahs_recording PUBLIC PkgConfig::GSTREAMER)
  target_compile_definitions(xerahs_recording PUBLIC XERAHS_GSTREAMER=1)
endif()
target_compile_options(xerahs_recording PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(xerahs PRIVATE xerahs_recording)
add_executable(recording-test tests/RecordingTest.cpp)
set_target_properties(recording-test PROPERTIES AUTOMOC ON)
target_link_libraries(recording-test PRIVATE xerahs_recording_core xerahs_core Qt6::Test)
add_test(NAME recording COMMAND recording-test)
if(XERAHS_RECORDING AND GSTREAMER_FOUND)
  add_executable(recording-integration-test tests/RecordingIntegrationTest.cpp)
  set_target_properties(recording-integration-test PROPERTIES AUTOMOC ON)
  target_link_libraries(recording-integration-test PRIVATE xerahs_recording_core PkgConfig::GSTREAMER Qt6::Test)
  add_test(NAME recording-integration COMMAND recording-integration-test)
  set_tests_properties(recording-integration PROPERTIES TIMEOUT 40)
endif()

add_executable(secret-service-credential-store-test tests/SecretServiceCredentialStoreTest.cpp)
set_target_properties(secret-service-credential-store-test PROPERTIES AUTOMOC ON)
target_link_libraries(secret-service-credential-store-test PRIVATE xerahs_secret_service Qt6::Test)
add_test(NAME secret-service-credential-store COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:secret-service-credential-store-test>)
set_tests_properties(secret-service-credential-store PROPERTIES TIMEOUT 30)

add_executable(destination-catalog-test tests/DestinationCatalogTest.cpp)
set_target_properties(destination-catalog-test PROPERTIES AUTOMOC ON)
target_link_libraries(destination-catalog-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME destination-catalog COMMAND destination-catalog-test)

add_library(xerahs_upload_destinations STATIC app/UploadDestinationsDialog.cpp app/UploadDestinationsDialog.h)
set_target_properties(xerahs_upload_destinations PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_upload_destinations PUBLIC app)
target_link_libraries(xerahs_upload_destinations PUBLIC xerahs_core Qt6::Widgets)
target_compile_options(xerahs_upload_destinations PRIVATE -Wall -Wextra -Wpedantic)
add_executable(upload-destinations-dialog-test tests/UploadDestinationsDialogTest.cpp)
set_target_properties(upload-destinations-dialog-test PROPERTIES AUTOMOC ON)
target_link_libraries(upload-destinations-dialog-test PRIVATE xerahs_upload_destinations Qt6::Test)
add_test(NAME upload-destinations-dialog COMMAND upload-destinations-dialog-test)
set_tests_properties(upload-destinations-dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 30)
add_executable(color-formats-test tests/ColorFormatsTest.cpp)
set_target_properties(color-formats-test PROPERTIES AUTOMOC ON)
# QColor lives in Qt Gui, which xerahs_core links privately.
target_link_libraries(color-formats-test PRIVATE xerahs_core Qt6::Gui Qt6::Test)
add_test(NAME color-formats COMMAND color-formats-test)
# CORE-020, CORE-021: pure preparation/parsing and native HTTP transport.
target_sources(xerahs_core PRIVATE
  src/uploads/UploadRequest.h src/uploads/UploadRequest.cpp
  src/uploads/UploadResponse.h src/uploads/UploadResponse.cpp)
add_library(xerahs_upload_executor STATIC src/uploads/UploadExecutor.h src/uploads/UploadExecutor.cpp)
set_target_properties(xerahs_upload_executor PROPERTIES AUTOMOC ON)
target_link_libraries(xerahs_upload_executor PUBLIC xerahs_core Qt6::Network)
foreach(surface Request Response Executor)
  string(TOLOWER "${surface}" surface_lower)
  add_executable(upload-${surface_lower}-test tests/Upload${surface}Test.cpp)
  set_target_properties(upload-${surface_lower}-test PROPERTIES AUTOMOC ON)
  target_link_libraries(upload-${surface_lower}-test PRIVATE xerahs_upload_executor Qt6::Test)
  add_test(NAME upload-${surface_lower} COMMAND upload-${surface_lower}-test)
endforeach()

# CORE-020, CORE-021: native capture upload integration.
target_link_libraries(xerahs PRIVATE xerahs_upload_executor xerahs_secret_service xerahs_upload_destinations)
foreach(upload_capture_target pipeline-journal-fixture pipeline-journal-test capture-pipeline-test
    action-selection-test capture-file-output-test capture-save-as-test capture-annotation-test
    capture-history-test capture-paired-save-test portal-screenshot-test native-capture-workflows-test workflow-shortcuts-test)
  target_link_libraries(${upload_capture_target} PRIVATE xerahs_upload_executor xerahs_secret_service)
endforeach()
add_executable(capture-upload-test tests/CaptureUploadTest.cpp app/CapturePipeline.cpp app/CapturePipeline.h
  app/CaptureActions.cpp app/CaptureActions.h app/CaptureSave.cpp app/CaptureSave.h)
set_target_properties(capture-upload-test PROPERTIES AUTOMOC ON)
target_link_libraries(capture-upload-test PRIVATE xerahs_file_output xerahs_action_selection xerahs_pipeline_journal
  xerahs_upload_executor xerahs_secret_service Qt6::Test)
add_test(NAME capture-upload COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:capture-upload-test>)
set_tests_properties(capture-upload PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
# POST-CAPTURE-ACTIONS-001: pin and print actions.
add_executable(capture-pin-print-test tests/CapturePinPrintTest.cpp app/CapturePipeline.cpp app/CapturePipeline.h
  app/CaptureActions.cpp app/CaptureActions.h app/CaptureSave.cpp app/CaptureSave.h)
set_target_properties(capture-pin-print-test PROPERTIES AUTOMOC ON)
target_link_libraries(capture-pin-print-test PRIVATE xerahs_file_output xerahs_action_selection xerahs_pipeline_journal
  xerahs_upload_executor xerahs_secret_service Qt6::Widgets Qt6::Test)
add_test(NAME capture-pin-print COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:capture-pin-print-test>)
set_tests_properties(capture-pin-print PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)
# INTERFACE-CATALOG-001: screen colour picker. Append-only; main.cpp wires the tray.
add_library(xerahs_color_picker STATIC
  app/PortalColorPicker.cpp
  app/PortalColorPicker.h
  app/ColorPickerDialog.cpp
  app/ColorPickerDialog.h)
set_target_properties(xerahs_color_picker PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_color_picker PUBLIC app)
target_link_libraries(xerahs_color_picker PUBLIC xerahs_core Qt6::Widgets Qt6::DBus)
target_compile_options(xerahs_color_picker PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(xerahs PRIVATE xerahs_color_picker)

add_executable(portal-color-picker-test tests/PortalColorPickerTest.cpp)
set_target_properties(portal-color-picker-test PROPERTIES AUTOMOC ON)
target_link_libraries(portal-color-picker-test PRIVATE xerahs_color_picker Qt6::Test)
add_test(NAME portal-color-picker COMMAND "${XERAHS_DBUS_RUN_SESSION}"
  "--config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf" -- $<TARGET_FILE:portal-color-picker-test>)
set_tests_properties(portal-color-picker PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 30)

add_executable(color-picker-dialog-test tests/ColorPickerDialogTest.cpp)
set_target_properties(color-picker-dialog-test PROPERTIES AUTOMOC ON)
target_link_libraries(color-picker-dialog-test PRIVATE xerahs_color_picker Qt6::Test)
add_test(NAME color-picker-dialog COMMAND color-picker-dialog-test)
set_tests_properties(color-picker-dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 30)
add_executable(sxcu-fixtures-test tests/SxcuFixturesTest.cpp)
set_target_properties(sxcu-fixtures-test PROPERTIES AUTOMOC ON)
target_compile_definitions(sxcu-fixtures-test PRIVATE XERAHS_SXCU_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sxcu")
target_link_libraries(sxcu-fixtures-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME sxcu-fixtures COMMAND sxcu-fixtures-test)

# CORE-020: the capture upload and Upload file share one catalog path.
target_sources(xerahs_core PRIVATE
  src/uploads/CatalogPath.h
  src/uploads/CatalogPath.cpp)

# CORE-020, CORE-023: upload an existing file from the tray or a window drop.
add_library(xerahs_file_upload STATIC
  app/FileUploadController.cpp
  app/FileUploadController.h
  app/FileUploadResultDialog.cpp
  app/FileUploadResultDialog.h)
set_target_properties(xerahs_file_upload PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_file_upload PUBLIC app)
target_link_libraries(xerahs_file_upload PUBLIC xerahs_upload_executor xerahs_secret_service Qt6::Widgets)
target_compile_options(xerahs_file_upload PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(xerahs PRIVATE xerahs_file_upload)

add_executable(file-upload-test tests/FileUploadTest.cpp app/ApplicationWindow.cpp app/ApplicationWindow.h
  app/CommandRegistry.cpp app/CommandRegistry.h)
set_target_properties(file-upload-test PROPERTIES AUTOMOC ON)
target_link_libraries(file-upload-test PRIVATE xerahs_file_upload Qt6::Test)
add_test(NAME file-upload COMMAND file-upload-test)
set_tests_properties(file-upload PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 30)

# EDITOR-UTILITIES-001 / EU-005: hash checker. Append-only; main.cpp wires the tray.
add_library(xerahs_hash_checker STATIC
  app/HashCheckerDialog.cpp
  app/HashCheckerDialog.h)
set_target_properties(xerahs_hash_checker PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_hash_checker PUBLIC app)
target_link_libraries(xerahs_hash_checker PUBLIC xerahs_core Qt6::Widgets)
target_compile_options(xerahs_hash_checker PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(xerahs PRIVATE xerahs_hash_checker)

add_executable(file-hash-test tests/FileHashTest.cpp)
set_target_properties(file-hash-test PROPERTIES AUTOMOC ON)
target_link_libraries(file-hash-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME file-hash COMMAND file-hash-test)

add_executable(hash-checker-dialog-test tests/HashCheckerDialogTest.cpp)
set_target_properties(hash-checker-dialog-test PROPERTIES AUTOMOC ON)
target_link_libraries(hash-checker-dialog-test PRIVATE xerahs_hash_checker Qt6::Test)
add_test(NAME hash-checker-dialog COMMAND hash-checker-dialog-test)
set_tests_properties(hash-checker-dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 30)
# CORE-024, CORE-025: native watch-folder consent and action dispatch.
add_library(xerahs_watch_folders STATIC
  app/WatchFolderService.cpp app/WatchFolderService.h
  app/WatchFoldersDialog.cpp app/WatchFoldersDialog.h)
set_target_properties(xerahs_watch_folders PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_watch_folders PUBLIC app)
target_link_libraries(xerahs_watch_folders PUBLIC xerahs_core xerahs_file_upload Qt6::Widgets)
target_compile_options(xerahs_watch_folders PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(xerahs PRIVATE xerahs_watch_folders)

add_executable(watch-folders-test tests/WatchFoldersTest.cpp)
set_target_properties(watch-folders-test PROPERTIES AUTOMOC ON)
target_link_libraries(watch-folders-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME watch-folders COMMAND watch-folders-test)
set_tests_properties(watch-folders PROPERTIES TIMEOUT 30)

add_executable(watch-folder-service-test tests/WatchFolderServiceTest.cpp)
set_target_properties(watch-folder-service-test PROPERTIES AUTOMOC ON)
target_link_libraries(watch-folder-service-test PRIVATE xerahs_watch_folders Qt6::Test)
add_test(NAME watch-folder-service COMMAND watch-folder-service-test)
set_tests_properties(watch-folder-service PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 30)

# CORE-004, EU-017: the shared owner-bound worker helper (header only).
add_executable(owned-work-test tests/OwnedWorkTest.cpp app/OwnedWork.h)
set_target_properties(owned-work-test PROPERTIES AUTOMOC ON)
target_include_directories(owned-work-test PRIVATE app)
target_link_libraries(owned-work-test PRIVATE Qt6::Core Qt6::Test)
add_test(NAME owned-work COMMAND owned-work-test)

# EDITOR-UTILITIES-001 / EU-012..EU-017: video converter. Model in xerahs_core; the dialog takes injected
# ports and always builds; the GStreamer runner links into xerahs only when GStreamer is found.
add_library(xerahs_video_converter_ui STATIC app/VideoConverterDialog.cpp app/VideoConverterDialog.h app/ConversionRunner.h)
set_target_properties(xerahs_video_converter_ui PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_video_converter_ui PUBLIC app)
target_link_libraries(xerahs_video_converter_ui PUBLIC xerahs_core Qt6::Widgets)
target_compile_options(xerahs_video_converter_ui PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(xerahs PRIVATE xerahs_video_converter_ui)
if(XERAHS_RECORDING AND GSTREAMER_FOUND)
  pkg_check_modules(GSTREAMER_PBUTILS QUIET IMPORTED_TARGET gstreamer-pbutils-1.0)
endif()
if(XERAHS_RECORDING AND GSTREAMER_FOUND AND GSTREAMER_PBUTILS_FOUND)
  add_library(xerahs_video_converter STATIC app/VideoConverter.cpp app/VideoConverter.h)
  set_target_properties(xerahs_video_converter PROPERTIES AUTOMOC ON)
  target_include_directories(xerahs_video_converter PUBLIC app)
  target_link_libraries(xerahs_video_converter PUBLIC xerahs_core xerahs_video_converter_ui PkgConfig::GSTREAMER PkgConfig::GSTREAMER_PBUTILS)
  target_compile_definitions(xerahs_video_converter PUBLIC XERAHS_VIDEO_CONVERTER=1)
  target_compile_options(xerahs_video_converter PRIVATE -Wall -Wextra -Wpedantic)
  target_link_libraries(xerahs PRIVATE xerahs_video_converter)
  target_link_libraries(application-window-test PRIVATE xerahs_video_converter)
endif()

add_executable(video-conversion-test tests/VideoConversionTest.cpp)
set_target_properties(video-conversion-test PROPERTIES AUTOMOC ON)
target_link_libraries(video-conversion-test PRIVATE xerahs_core Qt6::Test)
target_compile_definitions(video-conversion-test PRIVATE XERAHS_REPO_ROOT_DIR="${XERAHS_REPO_ROOT}")
add_test(NAME video-conversion COMMAND video-conversion-test)

add_executable(video-converter-dialog-test tests/VideoConverterDialogTest.cpp)
set_target_properties(video-converter-dialog-test PROPERTIES AUTOMOC ON)
target_link_libraries(video-converter-dialog-test PRIVATE xerahs_video_converter_ui Qt6::Test)
add_test(NAME video-converter-dialog COMMAND video-converter-dialog-test)
set_tests_properties(video-converter-dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 30)

if(TARGET xerahs_video_converter)
  add_executable(video-converter-integration-test tests/VideoConverterIntegrationTest.cpp)
  set_target_properties(video-converter-integration-test PROPERTIES AUTOMOC ON)
  target_link_libraries(video-converter-integration-test PRIVATE xerahs_video_converter Qt6::Test)
  add_test(NAME video-converter-integration COMMAND video-converter-integration-test)
  set_tests_properties(video-converter-integration PROPERTIES TIMEOUT 60)
  add_executable(gst-conversion-runner-lifetime-test tests/GstConversionRunnerLifetimeTest.cpp)
  set_target_properties(gst-conversion-runner-lifetime-test PROPERTIES AUTOMOC ON)
  target_link_libraries(gst-conversion-runner-lifetime-test PRIVATE xerahs_video_converter Qt6::Test)
  add_test(NAME gst-conversion-runner-lifetime COMMAND gst-conversion-runner-lifetime-test)
  set_tests_properties(gst-conversion-runner-lifetime PROPERTIES TIMEOUT 30)
endif()

# EDITOR-UTILITIES-001 / EU-007: icon converter. Append-only; main.cpp wires the tray.
add_library(xerahs_icon_converter STATIC
  app/IconWriter.cpp
  app/IconWriter.h
  app/IconConverterDialog.cpp
  app/IconConverterDialog.h)
set_target_properties(xerahs_icon_converter PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_icon_converter PUBLIC app)
target_link_libraries(xerahs_icon_converter PUBLIC xerahs_core Qt6::Widgets)
target_compile_options(xerahs_icon_converter PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(xerahs PRIVATE xerahs_icon_converter)
target_link_libraries(application-window-test PRIVATE xerahs_color_picker xerahs_hash_checker xerahs_icon_converter xerahs_video_converter_ui)

add_executable(icon-writer-test tests/IconWriterTest.cpp)
set_target_properties(icon-writer-test PROPERTIES AUTOMOC ON)
target_link_libraries(icon-writer-test PRIVATE xerahs_icon_converter Qt6::Test)
add_test(NAME icon-writer COMMAND icon-writer-test)
set_tests_properties(icon-writer PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 30)

add_executable(icon-converter-dialog-test tests/IconConverterDialogTest.cpp)
set_target_properties(icon-converter-dialog-test PROPERTIES AUTOMOC ON)
target_link_libraries(icon-converter-dialog-test PRIVATE xerahs_icon_converter Qt6::Test)
add_test(NAME icon-converter-dialog COMMAND icon-converter-dialog-test)
set_tests_properties(icon-converter-dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 30)
# SETCAT-005, FN-016, CORE-012, CORE-013: shared private JSON storage.
target_sources(xerahs_core PRIVATE src/storage/PrivateJsonFile.h src/storage/PrivateJsonFile.cpp)
add_executable(private-json-file-test tests/PrivateJsonFileTest.cpp)
set_target_properties(private-json-file-test PROPERTIES AUTOMOC ON)
target_link_libraries(private-json-file-test PRIVATE xerahs_core Qt6::Test)
add_test(NAME private-json-file COMMAND private-json-file-test)
# CORE-016: attempt bookkeeping for delayed and automatic capture.
add_executable(scheduled-capture-test tests/ScheduledCaptureTest.cpp
               app/ScheduledCapture.cpp app/ScheduledCapture.h
               app/DelayedCapture.cpp app/DelayedCapture.h
               app/AutoCaptureDialog.cpp app/AutoCaptureDialog.h)
set_target_properties(scheduled-capture-test PROPERTIES AUTOMOC ON)
target_include_directories(scheduled-capture-test PRIVATE app)
target_link_libraries(scheduled-capture-test PRIVATE xerahs_core Qt6::Widgets Qt6::Test)
add_test(NAME scheduled-capture COMMAND scheduled-capture-test)
set_tests_properties(scheduled-capture PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 30)

# EDITOR-UTILITIES-001 / EU-003: QR code generator. Append-only; main.cpp wires the tray.
target_sources(xerahs_core PRIVATE src/tools/QrEncoder.cpp src/tools/QrEncoder.h)
add_library(xerahs_qr_code STATIC
  app/QrRender.cpp
  app/QrRender.h
  app/QrCodeDialog.cpp
  app/QrCodeDialog.h)
set_target_properties(xerahs_qr_code PROPERTIES AUTOMOC ON)
target_include_directories(xerahs_qr_code PUBLIC app)
target_link_libraries(xerahs_qr_code PUBLIC xerahs_core Qt6::Widgets)
target_compile_options(xerahs_qr_code PRIVATE -Wall -Wextra -Wpedantic)
target_link_libraries(xerahs PRIVATE xerahs_qr_code)

add_executable(qr-encoder-test tests/QrEncoderTest.cpp)
set_target_properties(qr-encoder-test PROPERTIES AUTOMOC ON)
target_link_libraries(qr-encoder-test PRIVATE xerahs_qr_code Qt6::Gui Qt6::Test)
add_test(NAME qr-encoder COMMAND qr-encoder-test)
set_tests_properties(qr-encoder PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 120)

add_executable(qr-code-dialog-test tests/QrCodeDialogTest.cpp)
set_target_properties(qr-code-dialog-test PROPERTIES AUTOMOC ON)
target_link_libraries(qr-code-dialog-test PRIVATE xerahs_qr_code Qt6::Test)
add_test(NAME qr-code-dialog COMMAND qr-code-dialog-test)
set_tests_properties(qr-code-dialog PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORMTHEME=" TIMEOUT 30)

# CORE-020, CORE-023: existing-file receipts share the native pipeline journal.
target_link_libraries(xerahs_file_upload PUBLIC xerahs_pipeline_journal)

# EDITOR-EFFECTS-001: EE-001..EE-003, EE-008..EE-010, EE-016, EE-018.
add_library(xerahs_effects STATIC
  src/image-editor/effects/Effect.h
  src/image-editor/effects/Effect.cpp
  src/image-editor/effects/CensusCatalog.h
  src/image-editor/effects/CensusCatalog.cpp
  src/image-editor/effects/AdjustmentEffects.cpp
  src/image-editor/effects/AdjustmentEffects2.cpp
  src/image-editor/effects/EffectSupport.h
  src/image-editor/effects/EffectSupport.cpp
  src/image-editor/effects/DrawingEffects.cpp
  src/image-editor/effects/FilterEffects1.cpp
  src/image-editor/effects/ManipulationEffects2.cpp
  src/image-editor/effects/ManipulationEffects.cpp
  src/image-editor/effects/FilterEffects3.cpp
  src/image-editor/effects/FilterEffects2.cpp
  src/image-editor/effects/FilterEffects4.cpp)
target_include_directories(xerahs_effects PUBLIC src)
target_link_libraries(xerahs_effects PUBLIC Qt6::Core Qt6::Gui)
target_compile_options(xerahs_effects PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
target_link_libraries(xerahs PRIVATE xerahs_effects)

foreach(effect_test IN ITEMS EffectFramework CensusCatalog AdjustmentEffects AdjustmentEffects2 DrawingEffects FilterEffects1 ManipulationEffects ManipulationEffects2 FilterEffects3 FilterEffects2 FilterEffects4)
  add_executable(${effect_test}-test tests/${effect_test}Test.cpp)
  set_target_properties(${effect_test}-test PROPERTIES AUTOMOC ON)
  target_link_libraries(${effect_test}-test PRIVATE xerahs_effects Qt6::Test)
  add_test(NAME ${effect_test} COMMAND ${effect_test}-test)
endforeach()
target_link_libraries(application-window-test PRIVATE xerahs_qr_code)

# EDITOR-EFFECTS-001: owned browser, preview and guarded editor history apply.
target_sources(xerahs_editor_source PRIVATE app/EffectsDialog.cpp app/EffectsDialog.h)
set_target_properties(xerahs_editor_source PROPERTIES AUTOMOC ON)
target_link_libraries(xerahs_editor_source PUBLIC xerahs_effects)

add_executable(effects-dialog-test tests/EffectsDialogTest.cpp ${XERAHS_PREFERENCE_TEST_SOURCES})
set_target_properties(effects-dialog-test PROPERTIES AUTOMOC ON)
target_link_libraries(effects-dialog-test PRIVATE xerahs_editor_source xerahs_editor_canvas xerahs_editor_codecs Qt6::Widgets Qt6::DBus Qt6::Test)
add_test(NAME effects-dialog COMMAND ${XERAHS_DBUS_RUN_SESSION}
  --config-file=${CMAKE_CURRENT_SOURCE_DIR}/tests/appearance-test-bus.conf -- $<TARGET_FILE:effects-dialog-test>)
set_tests_properties(effects-dialog PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 30)

# EDITOR-EFFECTS-001: geometry effects reuse the core canvas pixel operations.
target_link_libraries(xerahs_effects PUBLIC xerahs_core)

# COMPAT-SXIE-001, EE-008, EE-015: memory-only legacy import and native presets.
target_sources(xerahs_effects PRIVATE
  src/image-editor/effects/SxieArchive.cpp src/image-editor/effects/SxieArchive.h
  src/image-editor/effects/SxiePreset.cpp src/image-editor/effects/SxiePreset.h)
target_link_libraries(xerahs_effects PRIVATE ZLIB::ZLIB)
target_sources(xerahs_editor_source PRIVATE app/PresetDialog.cpp app/PresetDialog.h)
add_executable(sxie-preset-test tests/SxiePresetTest.cpp ${XERAHS_PREFERENCE_TEST_SOURCES})
set_target_properties(sxie-preset-test PROPERTIES AUTOMOC ON)
target_compile_definitions(sxie-preset-test PRIVATE SXIE_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sxie/mapping.sxie")
target_link_libraries(sxie-preset-test PRIVATE xerahs_editor_source xerahs_editor_canvas xerahs_editor_codecs ZLIB::ZLIB Qt6::Widgets Qt6::DBus Qt6::Test)
add_test(NAME sxie-preset COMMAND sxie-preset-test)
set_tests_properties(sxie-preset PROPERTIES ENVIRONMENT QT_QPA_PLATFORM=offscreen TIMEOUT 60)

# COMPAT-SXIE-001: explicit legacy watermark and remaining effect mapping fixtures.
target_compile_definitions(sxie-preset-test PRIVATE SXIE_WATERMARK_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sxie/watermark.json")
