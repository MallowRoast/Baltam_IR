cmake_minimum_required(VERSION 3.16)

project(Baltam_IR LANGUAGES CXX)

include(CTest)

add_library(BALTAM_IR STATIC
    src/ir/ir_builder.cpp
    src/ir/ir_lowering.cpp
    src/ir/ir_print.cpp
)

target_include_directories(BALTAM_IR
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/core/include
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/core/include/bt_ast_interface
)

target_compile_features(BALTAM_IR PUBLIC cxx_std_17)

target_link_libraries(BALTAM_IR
    PUBLIC
        # 这里直接链接仓库内预置的共享库文件，依赖当前源码树布局；
        # 如果后续切到系统安装、包管理器或 find_package 方案，需要改成可迁移的发现方式。
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/core/lib/libbt_ast.so
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/core/lib/libbt_ast_base.so
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/logger/lib/libbt_logger.so
)

# 这里的 BUILD_RPATH 依赖源码树下的本地依赖目录，仅用于当前开发/测试环境。
# 如果以后改成安装产物运行，应该切换到 install rpath 或运行时显式注入库搜索路径。
set(CMAKE_BUILD_RPATH
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/core/lib"
    "${CMAKE_CURRENT_SOURCE_DIR}/deps/logger/lib")

if (BUILD_TESTING)
    add_subdirectory(test)
endif()
