cmake_minimum_required(VERSION 3.20)
project(YardaRegionBoundaryProbe LANGUAGES C CXX)

# R1 is a standalone experiment, not a dependency of the product build.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
find_package(LLVM 14 REQUIRED CONFIG)
find_package(GTest REQUIRED)
find_program(REGION_CLANG_EXECUTABLE NAMES clang-14 clang
    HINTS ${LLVM_TOOLS_BINARY_DIR} REQUIRED)
find_path(REGION_CLANG_INCLUDE_DIR clang/Lex/Preprocessor.h
    HINTS ${LLVM_INCLUDE_DIRS} REQUIRED)
find_library(REGION_CLANG_CPP NAMES clang-cpp libclang-cpp.so.14
    HINTS ${LLVM_LIBRARY_DIRS} REQUIRED)
find_library(REGION_LLVM NAMES LLVM-14 LLVM
    HINTS ${LLVM_LIBRARY_DIRS} REQUIRED)
set(REGION_APE_PLUGIN "${CMAKE_CURRENT_SOURCE_DIR}/../../../build/libMemoryAccessPatterns.so"
    CACHE FILEPATH "Existing whole-function MAP plugin used for compatibility checks")

add_library(region_pragma_probe MODULE pragma_probe.cpp)
target_include_directories(region_pragma_probe SYSTEM PRIVATE
    ${REGION_CLANG_INCLUDE_DIR} ${LLVM_INCLUDE_DIRS})
target_link_libraries(region_pragma_probe PRIVATE ${REGION_CLANG_CPP} ${REGION_LLVM})

add_library(region_capture STATIC region_capture.cpp)
target_include_directories(region_capture SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS})
target_link_libraries(region_capture PUBLIC ${REGION_LLVM})
add_executable(region_ir_probe ir_probe.cpp)
target_link_libraries(region_ir_probe PRIVATE region_capture)
add_executable(region_compile_probe compile_probe.cpp pragma_probe.cpp)
target_include_directories(region_compile_probe SYSTEM PRIVATE ${REGION_CLANG_INCLUDE_DIR})
target_link_libraries(region_compile_probe PRIVATE region_capture ${REGION_CLANG_CPP})
target_compile_definitions(region_compile_probe PRIVATE
    REGION_CLANG_EXECUTABLE="${REGION_CLANG_EXECUTABLE}")
add_executable(region_capture_tests region_capture_test.cpp)
target_link_libraries(region_capture_tests PRIVATE region_capture GTest::gtest)

foreach(target region_pragma_probe region_capture region_ir_probe region_compile_probe region_capture_tests)
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Werror)
endforeach()
enable_testing()
include(GoogleTest)
gtest_discover_tests(region_capture_tests)
add_test(NAME RegionBoundaryPipeline
    COMMAND ${CMAKE_COMMAND}
        -DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}
        -DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/observations
        -DPLUGIN=$<TARGET_FILE:region_pragma_probe>
        -DPROBE=$<TARGET_FILE:region_ir_probe>
        -DCOMPILER=$<TARGET_FILE:region_compile_probe>
        -DCLANG=${REGION_CLANG_EXECUTABLE}
        -DAPE_PLUGIN=${REGION_APE_PLUGIN}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/run_probe.cmake)
