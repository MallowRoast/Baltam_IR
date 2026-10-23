cmake_minimum_required(VERSION 3.16)
project(Baltam_IR VERSION 0.0.1 LANGUAGES CXX)

add_library(baltam_ir STATIC
    src/ir/ir_inst.cpp
    src/ir/ir_cache.cpp
    src/ir/ir_unit.cpp
    src/ir/world_counter.cpp)
target_compile_features(baltam_ir PUBLIC cxx_std_17)
target_include_directories(baltam_ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
