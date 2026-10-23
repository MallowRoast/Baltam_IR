cmake_minimum_required(VERSION 3.16)
project(Baltam_IR LANGUAGES CXX)

include(CTest)

add_library(baltam_ir STATIC
    src/ir/ir_inst.cpp
    src/ir/ir_module.cpp
    src/ir/ir_cache.cpp
    src/ir/ir_unit.cpp
    src/ir/world_counter.cpp)
target_compile_features(baltam_ir PUBLIC cxx_std_17)
target_include_directories(baltam_ir PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)

if(BUILD_TESTING)
    add_executable(ir_module_smoke test/ir_module_smoke.cpp)
    target_link_libraries(ir_module_smoke PRIVATE baltam_ir)
    add_test(NAME ir_module_smoke COMMAND ir_module_smoke)

    add_executable(ir_cache_smoke test/ir_cache_smoke.cpp)
    target_link_libraries(ir_cache_smoke PRIVATE baltam_ir)
    add_test(NAME ir_cache_smoke COMMAND ir_cache_smoke)

    add_executable(ir_index_smoke test/ir_index_smoke.cpp)
    target_link_libraries(ir_index_smoke PRIVATE baltam_ir)
    add_test(NAME ir_index_smoke COMMAND ir_index_smoke)

    add_executable(ir_handle_smoke test/ir_handle_smoke.cpp)
    target_link_libraries(ir_handle_smoke PRIVATE baltam_ir)
    add_test(NAME ir_handle_smoke COMMAND ir_handle_smoke)

    add_executable(ir_tables_smoke test/ir_tables_smoke.cpp)
    target_link_libraries(ir_tables_smoke PRIVATE baltam_ir)
    add_test(NAME ir_tables_smoke COMMAND ir_tables_smoke)
endif()
