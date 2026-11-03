cmake_minimum_required(VERSION 3.16)
project(Baltam_IR VERSION 0.1.3 LANGUAGES CXX)

set(BALTAM_AST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/core" CACHE PATH
    "Root directory of the Baltam core and AST SDK")
set(BALTAM_LOGGER_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/logger" CACHE PATH
    "Root directory of the Baltam logger SDK")
set(BALTAM_RUNTIME_LIBRARY_DIRS "" CACHE STRING
    "Additional runtime library directories (semicolon-separated)")

# 由 CMake 根据目标平台选择库文件命名规则。
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
    src/Transform/pass_manager.cpp
    src/Transform/mem2reg.cpp
    src/Analysis/ir_analysis.cpp
    src/ir/ir_io.cpp
    src/ir/ir_constant.cpp
    src/ir/ir_value.cpp
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

find_package(baltam_core REQUIRED COMPONENTS core HINTS "${BALTAM_AST_ROOT}")
find_package(MatTool 2.0.0 REQUIRED HINTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/mat_tool")
target_link_libraries(baltam_ir_core PUBLIC MatTool::MatTool)

option(BALTAM_BUILD_UTILS "Build the baltam_ir and opt command-line tools" ON)
if(BALTAM_BUILD_UTILS)
    add_executable(baltam_ir utils/baltam_ir.cpp)
    target_link_libraries(baltam_ir PRIVATE baltam_ir_core)
    add_executable(baltam_opt utils/opt.cpp)
    set_target_properties(baltam_opt PROPERTIES OUTPUT_NAME opt)
    target_link_libraries(baltam_opt PRIVATE baltam_ir_core)
endif()

# 格式化保持为独立目标，也可以显式开启构建前自动执行。
find_program(BALTAM_BASH_EXECUTABLE bash)
option(BALTAM_FORMAT_BEFORE_BUILD "构建前自动格式化项目 C/C++ 源码" OFF)
if(BALTAM_BASH_EXECUTABLE)
    add_custom_target(format
        COMMAND "${BALTAM_BASH_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/format-with-clang.sh"
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "格式化项目 C/C++ 源码"
        VERBATIM)
    add_custom_target(format-check
        COMMAND "${BALTAM_BASH_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/format-with-clang.sh" --check
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        COMMENT "检查项目 C/C++ 源码格式"
        VERBATIM)
    if(BALTAM_FORMAT_BEFORE_BUILD)
        add_dependencies(baltam_ir_core format)
    endif()
elseif(BALTAM_FORMAT_BEFORE_BUILD)
    message(FATAL_ERROR "构建前格式化需要安装 Bash")
endif()
