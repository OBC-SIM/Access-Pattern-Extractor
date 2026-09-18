find_program(YARDA_REGION_CLANG NAMES clang-14
    HINTS ${LLVM_TOOLS_BINARY_DIR} REQUIRED)
find_path(YARDA_CLANG_INCLUDE_DIR clang/Lex/Preprocessor.h
    HINTS ${LLVM_INCLUDE_DIRS} REQUIRED)
find_library(YARDA_CLANG_CPP NAMES libclang-cpp.so.14
    HINTS ${LLVM_LIBRARY_DIRS} REQUIRED)
find_library(YARDA_REGION_LLVM NAMES LLVM-14
    HINTS ${LLVM_LIBRARY_DIRS} REQUIRED)

add_library(YardaRegionFrontend STATIC
    src/region/RegionFrontendAction.cpp
    src/region/RegionPragmas.cpp
    src/region/RegionAstValidator.cpp
    src/region/RegionDriverOptions.cpp
    src/region/RegionOutput.cpp)
target_link_libraries(YardaRegionFrontend PUBLIC MapBuilders
    ${YARDA_CLANG_CPP} ${YARDA_REGION_LLVM})
target_include_directories(YardaRegionFrontend PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(YardaRegionFrontend SYSTEM PRIVATE
    ${YARDA_CLANG_INCLUDE_DIR} ${LLVM_INCLUDE_DIRS})
target_compile_definitions(YardaRegionFrontend PRIVATE
    YARDA_REGION_CLANG="${YARDA_REGION_CLANG}")
target_compile_options(YardaRegionFrontend PRIVATE -Wall -Wextra -Wpedantic)

add_executable(yarda_region_map src/region/main.cpp)
target_link_libraries(yarda_region_map PRIVATE YardaRegionFrontend)

if(GTEST_LIB)
    enable_testing()
    add_executable(YardaRegionFrontendTests
        tests/helpers/RegionTestMain.cpp
        tests/RegionFrontend_test.cpp
        tests/RegionFrontend_roles_test.cpp
        tests/RegionFrontend_access_test.cpp
        tests/RegionFrontend_validation_test.cpp)
    target_link_libraries(YardaRegionFrontendTests PRIVATE
        YardaRegionFrontend ${GTEST_LIB} pthread)
    add_test(NAME YardaRegionFrontendTests COMMAND YardaRegionFrontendTests)
    add_test(NAME RegionFrontendPipeline COMMAND ${CMAKE_COMMAND}
        "-DSOURCE_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/region-pipeline"
        "-DCOMPILER=$<TARGET_FILE:yarda_region_map>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_region_frontend.cmake")
    add_test(NAME RegionFrontendSelection COMMAND ${CMAKE_COMMAND}
        "-DWORK_DIR=${CMAKE_CURRENT_BINARY_DIR}/region-selection"
        "-DCOMPILER=$<TARGET_FILE:yarda_region_map>"
        -P "${CMAKE_CURRENT_SOURCE_DIR}/tests/run_region_selection.cmake")
endif()
