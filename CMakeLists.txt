cmake_minimum_required(VERSION 3.11)

project(TEST_KERNEL)

# cmake_policy(SET CMP0167 OLD)

add_executable(main
    src/main.cpp
    src/analysis/analysis_manager.cpp
    src/analysis/verifier.cpp
    src/ir/ir.cpp
    src/ir/ir_printer.cpp
    src/lowering/lowering_context.cpp
    src/lowering/lowering.cpp
    src/interpreter/interpreter.cpp
    src/optimizer/pass_manager.cpp
    src/optimizer/optimize.cpp
)
target_compile_definitions(main PRIVATE BALTAM_IR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(main PRIVATE src)

# boost
find_package(Boost REQUIRED)

find_package(baltam_core 3.8.4 REQUIRED COMPONENTS dev)
find_package(baltam_worker 3.8.4 REQUIRED)
find_package(baltam_kernel 3.10.2 REQUIRED)
find_library(BALTAM_LOGGER_LIBRARY
    NAMES bt_logger libbt_logger.so
    PATHS /opt/Baltamatica/lib
    NO_DEFAULT_PATH
    REQUIRED
)

target_link_libraries(main PRIVATE
    baltam_kernel::bt_ast
    baltam_worker::ba_worker
    ${BALTAM_LOGGER_LIBRARY}
)
target_link_options(main PRIVATE
    "-Wl,--disable-new-dtags"
    "-Wl,--no-as-needed"
)
set_target_properties(main PROPERTIES
    BUILD_RPATH "/home/zj/Desktop/Baltam_IR/deps/core/lib;/opt/Baltamatica/lib"
)
