cmake_minimum_required(VERSION 3.21)
project(XerahS LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# D-LIN-001: Qt 6, dynamically linked.
find_package(Qt6 6.4 REQUIRED COMPONENTS Core Test)

set(XERAHS_REPO_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../.." CACHE PATH "BriarForge/XerahS repository root")

add_library(xerahs_core STATIC
  src/actions/PostCapturePipeline.cpp
  src/actions/PostCapturePipeline.h
  src/naming/FilenameGenerator.cpp
  src/naming/FilenameGenerator.h
)
target_include_directories(xerahs_core PUBLIC src)
target_link_libraries(xerahs_core PUBLIC Qt6::Core)
target_compile_options(xerahs_core PRIVATE -Wall -Wextra -Wpedantic)

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

find_package(Python3 3.12 REQUIRED COMPONENTS Interpreter)
# Capabilities this edition implements. The full suite, including capabilities
# not yet implemented, runs without --capability and is expected to fail.
set(XERAHS_IMPLEMENTED_CAPABILITIES FILENAME-GENERATION-001 POST-CAPTURE-ACTIONS-001)
list(TRANSFORM XERAHS_IMPLEMENTED_CAPABILITIES PREPEND "--capability;" OUTPUT_VARIABLE XERAHS_CAPABILITY_ARGS)
add_test(NAME conformance
  COMMAND Python3::Interpreter "${XERAHS_REPO_ROOT}/conformance/runner/xerahs_conformance.py"
          --adapter $<TARGET_FILE:xerahs-conformance-adapter>
          --platform linux --edition linux-qt ${XERAHS_CAPABILITY_ARGS}
  WORKING_DIRECTORY "${XERAHS_REPO_ROOT}")
