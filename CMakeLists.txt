cmake_minimum_required(VERSION 3.16)
project(Baltam_IR VERSION 0.1.1 LANGUAGES CXX)

set(BALTAM_AST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/core" CACHE PATH
    "Root directory of the Baltam core and AST SDK")
set(BALTAM_LOGGER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/logger" CACHE PATH
    "Root directory of the Baltam logger SDK")
set(BALTAM_RUNTIME_LIBRARY_DIRS "" CACHE STRING
    "Additional runtime library directories (semicolon-separated)")

# Let CMake select the library naming convention for the target platform.
foreach(component core ast_base ast)
    find_library(BALTAM_${component}_LIBRARY NAMES bt_${component}
        PATHS "${BALTAM_AST_ROOT}/lib" NO_DEFAULT_PATH)
    if(NOT BALTAM_${component}_LIBRARY)
        message(FATAL_ERROR "Missing bt_${component} library in ${BALTAM_AST_ROOT}/lib")
    endif()
endforeach()
find_library(BALTAM_LOGGER_LIBRARY NAMES bt_logger
    PATHS "${BALTAM_LOGGER_ROOT}/lib" NO_DEFAULT_PATH)
if(NOT BALTAM_LOGGER_LIBRARY)
    message(FATAL_ERROR "Missing bt_logger library in ${BALTAM_LOGGER_ROOT}/lib")
endif()
if(NOT EXISTS "${BALTAM_AST_ROOT}/include/bt_ast_interface/bt_ast_interface.h")
    message(FATAL_ERROR "Missing AST headers in ${BALTAM_AST_ROOT}/include")
endif()

set(CMAKE_BUILD_RPATH
    "${BALTAM_AST_ROOT}/lib"
    "${BALTAM_LOGGER_ROOT}/lib"
    ${BALTAM_RUNTIME_LIBRARY_DIRS})

add_library(baltam_ir_core STATIC
    src/ir/ir_inst.cpp
    src/ir/ir_cache.cpp
    src/ir/ir_unit.cpp
    src/ir/world_counter.cpp
    src/ir/ir_builder.cpp)
target_compile_features(baltam_ir_core PUBLIC cxx_std_17)
target_include_directories(baltam_ir_core PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${BALTAM_AST_ROOT}/include"
    "${BALTAM_AST_ROOT}/include/bt_ast_interface")
target_link_libraries(baltam_ir_core PUBLIC
    "${BALTAM_core_LIBRARY}"
    "${BALTAM_ast_base_LIBRARY}"
    "${BALTAM_ast_LIBRARY}"
    "${BALTAM_LOGGER_LIBRARY}")

option(BALTAM_BUILD_UTILS "Build the baltam_ir command-line tool" ON)
if(BALTAM_BUILD_UTILS)
    add_executable(baltam_ir utils/baltam_ir.cpp)
    target_link_libraries(baltam_ir PRIVATE baltam_ir_core)
endif()

include(CTest)
option(BALTAM_ENABLE_AST_TEST "Build the M-file parser integration tests" OFF)
if(BUILD_TESTING)
    add_subdirectory(test)
endif()
