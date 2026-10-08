cmake_minimum_required(VERSION 3.21)
project(XerahS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# D-LIN-001: Qt 6, dynamically linked.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Test)
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
  src/image-editor/EditorSession.cpp
  src/image-editor/EditorSession.h
  src/naming/FilenameGenerator.cpp
  src/naming/FilenameGenerator.h
)
target_include_directories(xerahs_core PUBLIC src)
target_link_libraries(xerahs_core PUBLIC Qt6::Core PRIVATE ZLIB::ZLIB)
target_compile_options(xerahs_core PRIVATE -Wall -Wextra -Wpedantic
  # ES-025: binary64 in the order written, no fused multiply-add.
  -ffp-contract=off)

add_executable(xerahs-conformance-adapter conformance-adapter/main.cpp)
target_link_libraries(xerahs-conformance-adapter PRIVATE xerahs_core)
target_compile_options(xerahs-conformance-adapter PRIVATE -Wall -Wextra -Wpedantic)

enable_testing()
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

find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
# Capabilities this edition implements. The full suite, including capabilities
# not yet implemented, runs without --capability and is expected to fail.
set(XERAHS_IMPLEMENTED_CAPABILITIES FILENAME-GENERATION-001 POST-CAPTURE-ACTIONS-001 REGION-CAPTURE-001)
list(TRANSFORM XERAHS_IMPLEMENTED_CAPABILITIES PREPEND "--capability;" OUTPUT_VARIABLE XERAHS_CAPABILITY_ARGS)
add_test(NAME conformance
  COMMAND Python3::Interpreter "${XERAHS_REPO_ROOT}/conformance/runner/xerahs_conformance.py"
          --adapter $<TARGET_FILE:xerahs-conformance-adapter>
          --platform linux --edition linux-qt ${XERAHS_CAPABILITY_ARGS}
  WORKING_DIRECTORY "${XERAHS_REPO_ROOT}")
