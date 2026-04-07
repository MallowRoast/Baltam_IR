add_executable(cfg_analysis_test
    cfg_analysis_test.cpp
    ../src/analysis/analysis_manager.cpp
    ../src/analysis/cfg_analysis.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
)

target_include_directories(cfg_analysis_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME cfg_analysis_test COMMAND cfg_analysis_test)

add_executable(dominator_tree_test
    dominator_tree_test.cpp
    ../src/analysis/analysis_manager.cpp
    ../src/analysis/cfg_analysis.cpp
    ../src/analysis/dominator_tree.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
)

target_include_directories(dominator_tree_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME dominator_tree_test COMMAND dominator_tree_test)

add_executable(dominance_frontier_test
    dominance_frontier_test.cpp
    ../src/analysis/analysis_manager.cpp
    ../src/analysis/cfg_analysis.cpp
    ../src/analysis/dominance_frontier.cpp
    ../src/analysis/dominator_tree.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
)

target_include_directories(dominance_frontier_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME dominance_frontier_test COMMAND dominance_frontier_test)

add_executable(liveness_test
    liveness_test.cpp
    ../src/analysis/analysis_manager.cpp
    ../src/analysis/cfg_analysis.cpp
    ../src/analysis/liveness.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
)

target_include_directories(liveness_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME liveness_test COMMAND liveness_test)

add_executable(def_use_test
    def_use_test.cpp
    ../src/analysis/analysis_manager.cpp
    ../src/analysis/cfg_analysis.cpp
    ../src/analysis/def_use.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
)

target_include_directories(def_use_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME def_use_test COMMAND def_use_test)

add_executable(verifier_test
    verifier_test.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
)

target_include_directories(verifier_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME verifier_test COMMAND verifier_test)

add_executable(ir_printer_test
    ir_printer_test.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
    ../src/ir/ir_printer.cpp
)

target_include_directories(ir_printer_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME ir_printer_test COMMAND ir_printer_test)

add_executable(construct_untyped_ssa_test
    construct_untyped_ssa_test.cpp
    ../src/analysis/analysis_manager.cpp
    ../src/analysis/cfg_analysis.cpp
    ../src/analysis/def_use.cpp
    ../src/analysis/dominance_frontier.cpp
    ../src/analysis/dominator_tree.cpp
    ../src/analysis/liveness.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
    ../src/ir/ir_printer.cpp
    ../src/optimizer/construct_untyped_ssa.cpp
)

target_include_directories(construct_untyped_ssa_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME construct_untyped_ssa_test COMMAND construct_untyped_ssa_test)

add_executable(interpreter_test
    interpreter_test.cpp
    ../src/analysis/verifier.cpp
    ../src/interpreter/interpreter.cpp
    ../src/ir/ir.cpp
)

target_include_directories(interpreter_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(interpreter_test PRIVATE
    baltam_core::baltam_core
    baltam_worker::ba_worker
    ${BALTAM_LOGGER_LIBRARY}
)

target_link_options(interpreter_test PRIVATE
    "-Wl,--disable-new-dtags"
    "-Wl,--no-as-needed"
)

set_target_properties(interpreter_test PROPERTIES
    BUILD_RPATH "/home/zj/Desktop/Baltam_IR/deps/core/lib;/opt/Baltamatica/lib"
)

add_test(NAME interpreter_test COMMAND interpreter_test)

add_executable(simple_demo_test
    simple_demo_test.cpp
    ../src/analysis/analysis_manager.cpp
    ../src/analysis/cfg_analysis.cpp
    ../src/analysis/def_use.cpp
    ../src/analysis/dominance_frontier.cpp
    ../src/analysis/dominator_tree.cpp
    ../src/analysis/liveness.cpp
    ../src/analysis/verifier.cpp
    ../src/interpreter/interpreter.cpp
    ../src/ir/ir.cpp
    ../src/lowering/lowering.cpp
    ../src/optimizer/construct_untyped_ssa.cpp
)

target_compile_definitions(simple_demo_test PRIVATE
    BALTAM_IR_SOURCE_DIR="${CMAKE_SOURCE_DIR}"
)

target_include_directories(simple_demo_test PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)

target_link_libraries(simple_demo_test PRIVATE
    baltam_kernel::bt_ast
    baltam_worker::ba_worker
    ${BALTAM_LOGGER_LIBRARY}
)

target_link_options(simple_demo_test PRIVATE
    "-Wl,--disable-new-dtags"
    "-Wl,--no-as-needed"
)

set_target_properties(simple_demo_test PROPERTIES
    BUILD_RPATH "/home/zj/Desktop/Baltam_IR/deps/core/lib;/opt/Baltamatica/lib"
)

add_test(NAME simple_demo_test COMMAND simple_demo_test)
set_tests_properties(simple_demo_test PROPERTIES
    ENVIRONMENT "LD_LIBRARY_PATH=/opt/Baltamatica/lib:${CMAKE_SOURCE_DIR}/deps/core/lib"
)
