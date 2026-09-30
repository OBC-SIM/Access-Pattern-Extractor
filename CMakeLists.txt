cmake_minimum_required(VERSION 3.20)
project(MemoryAccessPatterns LANGUAGES C CXX)

find_package(LLVM 14 REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "LLVM_DIR: ${LLVM_DIR}")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

# ── LLVM Pass (shared library) ────────────────────────────
add_library(MapBuilders OBJECT
    src/AccessBuilder.cpp
    src/AccessMetadata.cpp
    src/AccessMetadataBuilder.cpp
    src/AccessPath.cpp
    src/AccessTypeInfo.cpp
    src/ArrayMetadata.cpp
    src/IrHelpers.cpp
    src/index/AffineExpression.cpp
    src/index/AffineResolution.cpp
    src/index/IndexResolution.cpp
    src/index/LoopInduction.cpp
    src/index/ScalarFormal.cpp
    src/StatementBuilder.cpp
    src/MapModuleBuilder.cpp
    src/InstructionCounts.cpp
    src/AnalysisRegion.cpp
    src/region/RegionLoopBounds.cpp
    src/region/RegionAccessBuilder.cpp
)
target_include_directories(MapBuilders PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(MapBuilders PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(MemoryAccessPatterns MODULE src/LoopAnalysisPass.cpp)
target_link_libraries(MemoryAccessPatterns PRIVATE MapBuilders)
target_include_directories(MemoryAccessPatterns PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
set_target_properties(MemoryAccessPatterns PROPERTIES
    CXX_VISIBILITY_PRESET default
    VISIBILITY_INLINES_HIDDEN NO
)
if(APPLE)
    set_target_properties(MemoryAccessPatterns PROPERTIES
        MACOSX_RPATH ON
        LINK_FLAGS "-undefined dynamic_lookup"
    )
else()
    target_link_options(MemoryAccessPatterns PRIVATE "-Wl,--allow-shlib-undefined")
endif()

# ── GTest 단위 테스트 ─────────────────────────────────────
llvm_map_components_to_libnames(LLVM_TEST_LIBS support core analysis)
find_library(GTEST_LIB      gtest      PATHS /usr/lib/x86_64-linux-gnu)
find_library(GTEST_MAIN_LIB gtest_main PATHS /usr/lib/x86_64-linux-gnu)

if(GTEST_LIB AND GTEST_MAIN_LIB)
    enable_testing()
    add_executable(MemoryAccessPatternsTests
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
        src/index/AffineExpression.cpp
        src/index/AffineResolution.cpp
        src/index/IndexResolution.cpp
        src/index/LoopInduction.cpp
        src/index/ScalarFormal.cpp
        src/region/RegionLoopBounds.cpp
    )
    target_include_directories(MemoryAccessPatternsTests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${LLVM_INCLUDE_DIRS}
    )
    target_link_libraries(MemoryAccessPatternsTests
        ${GTEST_LIB}
        ${GTEST_MAIN_LIB}
        ${LLVM_TEST_LIBS}
        pthread
    )
    add_test(NAME MemoryAccessPatternsTests COMMAND MemoryAccessPatternsTests)
    llvm_map_components_to_libnames(LLVM_REGION_TEST_LIBS asmparser passes)
    add_executable(YardaIndexTests tests/IndexResolution_test.cpp
        tests/IndexAffine_test.cpp
        tests/IndexRange_test.cpp tests/IndexBinding_test.cpp
        tests/IndexFormal_test.cpp tests/IndexScope_test.cpp
        tests/IndexProducer_test.cpp tests/helpers/RegionTestMain.cpp)
    target_link_libraries(YardaIndexTests PRIVATE
        MapBuilders ${GTEST_LIB} ${LLVM_REGION_TEST_LIBS} pthread)
    add_test(NAME YardaIndexTests COMMAND YardaIndexTests)
    add_executable(YardaRegionIrTests
        tests/helpers/RegionTestMain.cpp
        tests/InstructionCounts_test.cpp
        tests/AnalysisRegion_test.cpp)
    target_link_libraries(YardaRegionIrTests PRIVATE
        MapBuilders ${GTEST_LIB} ${LLVM_REGION_TEST_LIBS} pthread)
    add_test(NAME YardaRegionIrTests COMMAND YardaRegionIrTests)
    find_program(YARDA_REGION_OPT NAMES opt-14 HINTS ${LLVM_TOOLS_BINARY_DIR} REQUIRED)
    find_program(YARDA_INDEX_CLANG NAMES clang-14 HINTS ${LLVM_TOOLS_BINARY_DIR} REQUIRED)
    foreach(index "n*i" "i/2" "i*i" "i%2" "(signed char)(40*i)"
                  "(unsigned char)(64*i)" "n+1")
        string(REPLACE "*" "_mul_" case_name "${index}")
        string(REPLACE "/" "_div_" case_name "${case_name}")
        string(REPLACE "%" "_mod_" case_name "${case_name}")
        string(MAKE_C_IDENTIFIER "${case_name}" case_name)
        if(index STREQUAL "n*i")
            set(reason "runtime-dependent coefficient")
        elseif(index STREQUAL "i*i")
            set(reason "nonlinear recurrence")
        elseif(index STREQUAL "i/2")
            set(reason "expression is outside the affine grammar")
        elseif(index STREQUAL "i%2")
            # SCEV can lower remainder to a narrow recurrence; its rejection
            # suffix depends on the proved domain. Require the common prefix.
            set(reason "")
        else()
            set(reason "index may wrap or narrow")
        endif()
        add_test(NAME "LegacyAffineRejection_${case_name}" COMMAND ${CMAKE_COMMAND}
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/legacy-affine-${case_name}"
            "-DCLANG=${YARDA_INDEX_CLANG}" "-DOPT=${YARDA_REGION_OPT}"
            "-DPLUGIN=$<TARGET_FILE:MemoryAccessPatterns>"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/legacy_affine_rejected.c"
            "-DINDEX=${index}"
            "-DREASON=${reason}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_legacy_affine.cmake")
    endforeach()
    foreach(index "(long)p" "(long)p & 7" "(long)p * 2" "(long)(p+i)" "(long)p+i")
        string(MAKE_C_IDENTIFIER "${index}" case_name)
        add_test(NAME "LegacyPointerIndexRejection_${case_name}" COMMAND ${CMAKE_COMMAND}
            "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/legacy-pointer-index-${case_name}"
            "-DCLANG=${YARDA_INDEX_CLANG}" "-DOPT=${YARDA_REGION_OPT}"
            "-DPLUGIN=$<TARGET_FILE:MemoryAccessPatterns>"
            "-DSOURCE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/legacy_pointer_index_rejected.c"
            "-DINDEX=${index}"
            "-DREASON=expression is outside the affine grammar"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_legacy_affine.cmake")
    endforeach()
    add_test(NAME LegacyRegionRejection COMMAND ${CMAKE_COMMAND}
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/legacy-region-rejection"
        "-DOPT=${YARDA_REGION_OPT}"
        "-DPLUGIN=$<TARGET_FILE:MemoryAccessPatterns>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_legacy_region_rejection.cmake")
    add_test(NAME StructFixtureTrace
        COMMAND sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_struct_fixture.sh
            ${CMAKE_CURRENT_SOURCE_DIR}
            $<TARGET_FILE:MemoryAccessPatterns>
    )
    add_test(NAME PointerIndirectionFixtureTrace
        COMMAND sh
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/run_pointer_indirection_fixture.sh
            ${CMAKE_CURRENT_SOURCE_DIR}
            $<TARGET_FILE:MemoryAccessPatterns>
    )
else()
    message(WARNING "GTest not found — skipping unit tests")
endif()

option(YARDA_BUILD_REGION_FRONTEND "Build the fixed Clang 14 region frontend" OFF)
if(YARDA_BUILD_REGION_FRONTEND)
    include(cmake/RegionFrontend.cmake)
endif()
