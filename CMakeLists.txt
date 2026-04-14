cmake_minimum_required(VERSION 3.11)

project(BALTAM_IR)

option(BUILD_TEST "Build unit tests." ON)

if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/core")
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/core")
endif()
if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/deps/logger")
    list(PREPEND CMAKE_PREFIX_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/logger")
endif()

set(BALTAM_IR_LIBRARY_SOURCES
    src/analysis/analysis_manager.cpp
    src/analysis/cfg_analysis.cpp
    src/analysis/def_use.cpp
    src/analysis/dominance_frontier.cpp
    src/analysis/dominator_tree.cpp
    src/analysis/liveness.cpp
    src/analysis/value_def.cpp
    src/analysis/verifier.cpp
    src/interpreter/interpreter.cpp
    src/ir/ir.cpp
    src/ir/ir_printer.cpp
    src/lowering/lowering.cpp
    src/optimizer/constant_fold.cpp
    src/optimizer/construct_untyped_ssa.cpp
    src/optimizer/dce.cpp
    src/optimizer/pass_manager.cpp
    src/optimizer/optimize.cpp
)
add_library(BALTAM_IR ${BALTAM_IR_LIBRARY_SOURCES})
add_library(BALTAM_IR::BALTAM_IR ALIAS BALTAM_IR)
target_include_directories(BALTAM_IR PUBLIC src)

find_package(Boost REQUIRED)
find_package(baltam_logger 1.0.4 REQUIRED CONFIG)
find_package(baltam_core 3.8.4 REQUIRED COMPONENTS dev)
find_package(baltam_worker 3.8.4 REQUIRED)
find_package(baltam_kernel 3.10.2 REQUIRED)

target_link_libraries(BALTAM_IR PUBLIC
    baltam_kernel::bt_ast
    baltam_worker::ba_worker
    baltam_logger::baltam_logger
)

if(BUILD_TEST)
    enable_testing()
    add_subdirectory(test)
endif()
