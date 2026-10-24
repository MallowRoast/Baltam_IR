cmake_minimum_required(VERSION 3.16)
project(Baltam_IR VERSION 0.1.1 LANGUAGES CXX)

add_library(baltam_ir STATIC
    src/ir/ir_inst.cpp
    src/ir/ir_cache.cpp
    src/ir/ir_unit.cpp
    src/ir/world_counter.cpp)
target_compile_features(baltam_ir PUBLIC cxx_std_17)
target_include_directories(baltam_ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_include_directories(baltam_ir PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/deps/core/include)
target_link_libraries(baltam_ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/deps/core/lib/libbt_core.so)

include(CTest)
option(BALTAM_ENABLE_AST_TEST "Build the M-file parser integration test" OFF)
option(BALTAM_BUILD_UTILS "Build the baltam_ir command-line tool" ON)
if(BALTAM_BUILD_UTILS OR (BUILD_TESTING AND BALTAM_ENABLE_AST_TEST))
    set(BALTAM_AST_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/deps/core" CACHE PATH
        "Root directory of the Baltam AST SDK")
    foreach(required_file
        include/bt_ast_interface/bt_ast_interface.h
        lib/libbt_ast_base.so)
        if(NOT EXISTS "${BALTAM_AST_ROOT}/${required_file}")
            message(FATAL_ERROR "IR builder requires ${BALTAM_AST_ROOT}/${required_file}")
        endif()
    endforeach()
    add_library(baltam_ir_builder STATIC src/ir/ir_builder.cpp)
    target_compile_features(baltam_ir_builder PUBLIC cxx_std_17)
    target_link_libraries(baltam_ir_builder PUBLIC baltam_ir
        ${BALTAM_AST_ROOT}/lib/libbt_ast_base.so
        ${BALTAM_AST_ROOT}/lib/libbt_ast.so)
    target_include_directories(baltam_ir_builder PUBLIC
        ${BALTAM_AST_ROOT}/include
        ${BALTAM_AST_ROOT}/include/bt_ast_interface)
endif()

if(BALTAM_BUILD_UTILS)
    if(NOT EXISTS "${BALTAM_AST_ROOT}/lib/libbt_ast.so")
        message(FATAL_ERROR "baltam_ir requires ${BALTAM_AST_ROOT}/lib/libbt_ast.so")
    endif()
    add_executable(baltam_ir_cli utils/baltam_ir.cpp)
    set_target_properties(baltam_ir_cli PROPERTIES OUTPUT_NAME baltam_ir BUILD_RPATH
        "${BALTAM_AST_ROOT}/lib;${CMAKE_CURRENT_SOURCE_DIR}/deps/logger/lib")
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND UNIX AND NOT APPLE)
        # 将 SDK 的 logger 依赖直接列入可执行文件，保证运行时能定位它。
        target_link_options(baltam_ir_cli PRIVATE -Wl,--no-as-needed)
        target_link_libraries(baltam_ir_cli PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/deps/logger/lib/libbt_logger.so)
    endif()
    target_link_libraries(baltam_ir_cli PRIVATE
        baltam_ir_builder ${BALTAM_AST_ROOT}/lib/libbt_ast.so)
endif()

if(BUILD_TESTING)
    add_subdirectory(test)
endif()
