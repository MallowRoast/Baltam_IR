function(add_baltam_ir_test test_name)
    add_executable(${test_name} ${ARGN})
    target_include_directories(${test_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/src
    )
    add_test(NAME ${test_name} COMMAND $<TARGET_FILE:${test_name}>)
endfunction()

function(configure_baltam_runtime_binary target_name)
    target_link_options(${target_name} PRIVATE
        "-Wl,--disable-new-dtags"
        "-Wl,--no-as-needed"
    )
endfunction()

add_baltam_ir_test(cfg_analysis_test
    cfg_analysis_test.cpp
    ../src/analysis/analysis_manager.cpp
    ../src/analysis/cfg_analysis.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
)

add_baltam_ir_test(dominator_tree_test
    dominator_tree_test.cpp
    ../src/analysis/analysis_manager.cpp
    ../src/analysis/cfg_analysis.cpp
    ../src/analysis/dominator_tree.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
)

add_baltam_ir_test(dominance_frontier_test
    dominance_frontier_test.cpp
    ../src/analysis/analysis_manager.cpp
    ../src/analysis/cfg_analysis.cpp
    ../src/analysis/dominance_frontier.cpp
    ../src/analysis/dominator_tree.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
)

add_baltam_ir_test(liveness_test
    liveness_test.cpp
    ../src/analysis/analysis_manager.cpp
    ../src/analysis/cfg_analysis.cpp
    ../src/analysis/liveness.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
)

add_baltam_ir_test(def_use_test
    def_use_test.cpp
    ../src/analysis/analysis_manager.cpp
    ../src/analysis/cfg_analysis.cpp
    ../src/analysis/def_use.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
)

add_baltam_ir_test(value_def_test
    value_def_test.cpp
    ../src/analysis/analysis_manager.cpp
    ../src/analysis/value_def.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
)

add_baltam_ir_test(verifier_test
    verifier_test.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
)

add_baltam_ir_test(ir_printer_test
    ir_printer_test.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
    ../src/ir/ir_printer.cpp
)

add_baltam_ir_test(construct_untyped_ssa_test
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

add_baltam_ir_test(constant_fold_test
    constant_fold_test.cpp
    ../src/analysis/analysis_manager.cpp
    ../src/analysis/value_def.cpp
    ../src/analysis/verifier.cpp
    ../src/ir/ir.cpp
    ../src/ir/ir_printer.cpp
    ../src/optimizer/constant_fold.cpp
    ../src/optimizer/pass_manager.cpp
    ../src/optimizer/optimize.cpp
)

add_baltam_ir_test(interpreter_test
    interpreter_test.cpp
    ../src/analysis/verifier.cpp
    ../src/interpreter/interpreter.cpp
    ../src/ir/ir.cpp
)

target_link_libraries(interpreter_test PRIVATE
    baltam_core::baltam_core
    baltam_worker::ba_worker
    baltam_logger::baltam_logger
)

configure_baltam_runtime_binary(interpreter_test)

set(BALTAM_IR_TEST_LD_LIBRARY_PATH "${CMAKE_SOURCE_DIR}/deps/core/lib")
set(BALTAM_IR_BUILTIN_LIBRARY_DIR "")
foreach(candidate_dir IN ITEMS "$ENV{BALTAMATICA_LIB_DIR}" "$ENV{BALTAM_LIB_DIR}" "/opt/Baltamatica/lib")
    if(candidate_dir AND EXISTS "${candidate_dir}/libbt_builtin.so")
        set(BALTAM_IR_BUILTIN_LIBRARY_DIR "${candidate_dir}")
        break()
    endif()
endforeach()
if(BALTAM_IR_BUILTIN_LIBRARY_DIR)
    string(APPEND BALTAM_IR_TEST_LD_LIBRARY_PATH ":${BALTAM_IR_BUILTIN_LIBRARY_DIR}")
endif()
if(DEFINED ENV{LD_LIBRARY_PATH} AND NOT "$ENV{LD_LIBRARY_PATH}" STREQUAL "")
    string(APPEND BALTAM_IR_TEST_LD_LIBRARY_PATH ":$ENV{LD_LIBRARY_PATH}")
endif()

function(configure_baltam_runtime_test test_name)
    set_tests_properties(${test_name} PROPERTIES
        ENVIRONMENT
            "BALTAM_FRONTEND=console;LD_LIBRARY_PATH=${BALTAM_IR_TEST_LD_LIBRARY_PATH}"
    )
endfunction()

configure_baltam_runtime_test(interpreter_test)

function(add_baltam_ir_m_script_test source_file)
    get_filename_component(test_name "${source_file}" NAME_WE)

    add_baltam_ir_test(${test_name}
        "${source_file}"
    )
    target_compile_definitions(${test_name} PRIVATE
        BALTAM_IR_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(${test_name} PRIVATE
        BALTAM_IR
    )
    configure_baltam_runtime_binary(${test_name})
    configure_baltam_runtime_test(${test_name})
    set_tests_properties(${test_name} PROPERTIES
        LABELS "m_script"
    )
endfunction()

# m 脚本回归测试统一放在 test_*.cpp 里；新增脚本适配时只需新增一个对应源文件。
if(CMAKE_VERSION VERSION_GREATER_EQUAL "3.12")
    file(GLOB BALTAM_IR_M_SCRIPT_TEST_SOURCES CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp"
    )
else()
    file(GLOB BALTAM_IR_M_SCRIPT_TEST_SOURCES
        "${CMAKE_CURRENT_SOURCE_DIR}/test_*.cpp"
    )
endif()
list(SORT BALTAM_IR_M_SCRIPT_TEST_SOURCES)

foreach(m_script_test_source IN LISTS BALTAM_IR_M_SCRIPT_TEST_SOURCES)
    add_baltam_ir_m_script_test("${m_script_test_source}")
endforeach()
