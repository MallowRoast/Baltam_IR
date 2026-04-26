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
        # 当前统一链接本机 `/opt/Baltamatica/lib` 下的运行库，避免和仓库 `deps/`
        # 目录中的旧版 `.so` 混用。`deps/` 只保留头文件用途。
        /opt/Baltamatica/lib/libbt_ast.so
        /opt/Baltamatica/lib/libbt_ast_base.so
        /opt/Baltamatica/lib/libbt_logger.so
)

# 这里统一指向 `/opt/Baltamatica/lib`，保证主库与后续 builtin/plugin 使用同一套 runtime。
set(CMAKE_BUILD_RPATH
    "/opt/Baltamatica/lib")

if (BUILD_TESTING)
    add_subdirectory(test)
endif()
