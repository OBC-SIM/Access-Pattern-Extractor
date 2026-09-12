cmake_minimum_required(VERSION 3.20)
project(LoopAnnotatedTrace LANGUAGES C CXX)

find_package(LLVM 14 REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM_DIR: ${LLVM_DIR}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# ── LLVM Pass (shared library) ────────────────────────────
add_library(LatBuilders OBJECT
    src/AccessBuilder.cpp
    src/AccessMetadata.cpp
    src/AccessMetadataBuilder.cpp
    src/AccessPath.cpp
    src/AccessTypeInfo.cpp
    src/ArrayMetadata.cpp
    src/IrHelpers.cpp
    src/index/IndexResolution.cpp
    src/index/LoopInduction.cpp
    src/index/ScalarFormal.cpp
    src/StatementBuilder.cpp
    src/LatModuleBuilder.cpp
    src/AnalysisRegion.cpp
    src/region/RegionLoopBounds.cpp
    src/region/RegionAccessBuilder.cpp
)
target_include_directories(LatBuilders PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(LatBuilders PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(LoopAnnotatedTrace MODULE src/LoopAnalysisPass.cpp)
target_link_libraries(LoopAnnotatedTrace PRIVATE LatBuilders)
target_include_directories(LoopAnnotatedTrace PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_target_properties(LoopAnnotatedTrace PROPERTIES
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN NO
)
if(APPLE)
    set_target_properties(LoopAnnotatedTrace PROPERTIES
        MACOSX_RPATH ON
        LINK_FLAGS "-undefined dynamic_lookup"
    )
else()
    target_link_options(LoopAnnotatedTrace PRIVATE "-Wl,--allow-shlib-undefined")
endif()

# ── GTest 단위 테스트 ─────────────────────────────────────
llvm_map_components_to_libnames(LLVM_TEST_LIBS support core analysis)
find_library(GTEST_LIB      gtest      PATHS /usr/lib/x86_64-linux-gnu)
find_library(GTEST_MAIN_LIB gtest_main PATHS /usr/lib/x86_64-linux-gnu)

if(GTEST_LIB AND GTEST_MAIN_LIB)
    enable_testing()
    add_executable(LoopAnnotatedTraceTests
        tests/Statement_test.cpp
        tests/CallStmt_test.cpp
        tests/AccessBuilder_test.cpp
        tests/AccessObjectBinding_test.cpp
        tests/AccessMetadataBuilder_test.cpp
        tests/AccessPath_test.cpp
        tests/IrHelpers_test.cpp
        tests/IrHelpersGep_test.cpp
        tests/ArrayMetadata_test.cpp
        src/AccessMetadata.cpp
        src/AccessMetadataBuilder.cpp
        src/AccessPath.cpp
        src/AccessBuilder.cpp
        src/AccessTypeInfo.cpp
        src/ArrayMetadata.cpp
        src/IrHelpers.cpp
        src/index/IndexResolution.cpp
        src/index/LoopInduction.cpp
        src/index/ScalarFormal.cpp
        src/region/RegionLoopBounds.cpp
    )
    target_include_directories(LoopAnnotatedTraceTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LLVM_INCLUDE_DIRS}
    )
    target_link_libraries(LoopAnnotatedTraceTests
        ${GTEST_LIB}
        ${GTEST_MAIN_LIB}
        ${LLVM_TEST_LIBS}
        pthread
    )
    add_test(NAME LoopAnnotatedTraceTests COMMAND LoopAnnotatedTraceTests)
    llvm_map_components_to_libnames(LLVM_REGION_TEST_LIBS asmparser passes)
    add_executable(YardaRegionIrTests
        tests/helpers/RegionTestMain.cpp
        tests/AnalysisRegion_test.cpp)
    target_link_libraries(YardaRegionIrTests PRIVATE
        LatBuilders ${GTEST_LIB} ${LLVM_REGION_TEST_LIBS} pthread)
    add_test(NAME YardaRegionIrTests COMMAND YardaRegionIrTests)
    find_program(YARDA_REGION_OPT NAMES opt-14 HINTS ${LLVM_TOOLS_BINARY_DIR} REQUIRED)
    add_test(NAME LegacyRegionRejection COMMAND ${CMAKE_COMMAND}
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/legacy-region-rejection"
        "-DOPT=${YARDA_REGION_OPT}"
        "-DPLUGIN=$<TARGET_FILE:LoopAnnotatedTrace>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_legacy_region_rejection.cmake")
    add_test(NAME StructFixtureTrace
        COMMAND sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_struct_fixture.sh
            ${CMAKE_CURRENT_SOURCE_DIR}
            $<TARGET_FILE:LoopAnnotatedTrace>
    )
    add_test(NAME PointerIndirectionFixtureTrace
        COMMAND sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_pointer_indirection_fixture.sh
            ${CMAKE_CURRENT_SOURCE_DIR}
            $<TARGET_FILE:LoopAnnotatedTrace>
    )
else()
    message(WARNING "GTest not found — skipping unit tests")
endif()

option(YARDA_BUILD_REGION_FRONTEND "Build the fixed Clang 14 region frontend" OFF)
if(YARDA_BUILD_REGION_FRONTEND)
    include(cmake/RegionFrontend.cmake)
endif()
