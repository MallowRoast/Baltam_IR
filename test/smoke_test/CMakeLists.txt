function(add_ir_smoke_test target source macro_name mfile_relpath)
    add_executable(${target}
        ${source}
    )

    target_compile_definitions(${target}
        PRIVATE
            ${macro_name}="${CMAKE_SOURCE_DIR}/${mfile_relpath}"
    )

    target_include_directories(${target}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )

    # smoke test 统一使用 `/opt/Baltamatica/lib` 下的 runtime，避免与仓库 `deps/`
    # 目录中的旧版共享库混用。
    set_target_properties(${target} PROPERTIES
        BUILD_RPATH "/opt/Baltamatica/lib"
    )

    target_link_libraries(${target}
        PRIVATE
            BALTAM_IR
    )

    add_test(
        NAME ${target}
        COMMAND ${target}
    )

    # 这里额外注入 `HOME=/tmp`、`XDG_CACHE_HOME=/tmp/.cache` 和 `/opt` 下的运行库路径，
    # 以便测试进程与 builtin/plugin 使用同一套运行时环境。
    set_tests_properties(${target} PROPERTIES
        ENVIRONMENT
            "HOME=/tmp;XDG_CACHE_HOME=/tmp/.cache;BALTAM_FRONTEND=console;LD_LIBRARY_PATH=/opt/Baltamatica/lib"
    )
endfunction()

function(set_smoke_labels target)
    set_tests_properties(${target} PROPERTIES
        LABELS "${ARGN}"
    )
endfunction()

add_ir_smoke_test(
    test0_smoke
    syntax/test0_smoke.cpp
    TEST0_MFILE_PATH
    test/m/test0/test0.m
)
set_smoke_labels(test0_smoke smoke syntax)

add_ir_smoke_test(
    test0_1_smoke
    syntax/test0_1_smoke.cpp
    TEST0_1_MFILE_PATH
    test/m/test0/test0_1.m
)
set_smoke_labels(test0_1_smoke smoke syntax)

add_ir_smoke_test(
    test1_smoke
    syntax/test1_smoke.cpp
    TEST1_MFILE_PATH
    test/m/test1/test1.m
)
set_smoke_labels(test1_smoke smoke syntax)

add_ir_smoke_test(
    test1_1_smoke
    syntax/test1_1_smoke.cpp
    TEST1_1_MFILE_PATH
    test/m/test1/test1_1.m
)
set_smoke_labels(test1_1_smoke smoke syntax)

add_ir_smoke_test(
    test2_smoke
    syntax/test2_smoke.cpp
    TEST2_MFILE_PATH
    test/m/test2/test2.m
)
set_smoke_labels(test2_smoke smoke syntax)

add_ir_smoke_test(
    test2_1_smoke
    syntax/test2_1_smoke.cpp
    TEST2_1_MFILE_PATH
    test/m/test2/test2_1.m
)
set_smoke_labels(test2_1_smoke smoke syntax)

add_ir_smoke_test(
    test2_2_smoke
    syntax/test2_2_smoke.cpp
    TEST2_2_MFILE_PATH
    test/m/test2/test2_2.m
)
set_smoke_labels(test2_2_smoke smoke syntax)

add_ir_smoke_test(
    test2_3_smoke
    syntax/test2_3_smoke.cpp
    TEST2_3_MFILE_PATH
    test/m/test2/test2_3.m
)
set_smoke_labels(test2_3_smoke smoke syntax)

add_ir_smoke_test(
    test3_smoke
    syntax/test3_smoke.cpp
    TEST3_MFILE_PATH
    test/m/test3/test3.m
)
set_smoke_labels(test3_smoke smoke syntax)

add_ir_smoke_test(
    test3_1_smoke
    syntax/test3_1_smoke.cpp
    TEST3_1_MFILE_PATH
    test/m/test3/test3_1.m
)
set_smoke_labels(test3_1_smoke smoke syntax)

add_ir_smoke_test(
    test3_2_smoke
    syntax/test3_2_smoke.cpp
    TEST3_2_MFILE_PATH
    test/m/test3/test3_2.m
)
set_smoke_labels(test3_2_smoke smoke syntax)

add_ir_smoke_test(
    test3_3_smoke
    syntax/test3_3_smoke.cpp
    TEST3_3_MFILE_PATH
    test/m/test3/test3_3.m
)
set_smoke_labels(test3_3_smoke smoke syntax)

add_ir_smoke_test(
    test4_smoke
    syntax/test4_smoke.cpp
    TEST4_MFILE_PATH
    test/m/test4/test4.m
)
set_smoke_labels(test4_smoke smoke syntax)

add_ir_smoke_test(
    test4_1_smoke
    syntax/test4_1_smoke.cpp
    TEST4_1_MFILE_PATH
    test/m/test4/test4_1.m
)
set_smoke_labels(test4_1_smoke smoke syntax)

add_ir_smoke_test(
    test4_2_smoke
    syntax/test4_2_smoke.cpp
    TEST4_2_MFILE_PATH
    test/m/test4/test4_2.m
)
set_smoke_labels(test4_2_smoke smoke syntax)

add_ir_smoke_test(
    test4_3_smoke
    syntax/test4_3_smoke.cpp
    TEST4_3_MFILE_PATH
    test/m/test4/test4_3.m
)
set_smoke_labels(test4_3_smoke smoke syntax)

add_ir_smoke_test(
    test5_1_smoke
    syntax/test5_1_smoke.cpp
    TEST5_1_MFILE_PATH
    test/m/test5/test5_1.m
)
set_smoke_labels(test5_1_smoke smoke syntax)

add_executable(ir_type_smoke
    feature/ir_type_smoke.cpp
)

target_include_directories(ir_type_smoke
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(ir_type_smoke
    PRIVATE
        BALTAM_IR
)

add_test(
    NAME ir_type_smoke
    COMMAND ir_type_smoke
)
set_smoke_labels(ir_type_smoke smoke feature)

add_ir_smoke_test(
    cfg_dot_smoke
    feature/cfg_dot_smoke.cpp
    TEST3_3_MFILE_PATH
    test/m/test3/test3_3.m
)
target_sources(cfg_dot_smoke
    PRIVATE
        ${CMAKE_SOURCE_DIR}/tools/cfg_dot.cpp
)
target_include_directories(cfg_dot_smoke
    PRIVATE
        ${CMAKE_SOURCE_DIR}
)
set_smoke_labels(cfg_dot_smoke smoke feature)

set(IR_CFG_DOT_CLI_SMOKE_DOT "${CMAKE_BINARY_DIR}/test/cfg_dot_cli/test3_3.dot")
set(IR_CFG_DOT_CLI_SMOKE_SVG "${CMAKE_BINARY_DIR}/test/cfg_dot_cli/test3_3.svg")
set(IR_CFG_DOT_CLI_SMOKE_PNG "${CMAKE_BINARY_DIR}/test/cfg_dot_cli/test3_3.png")

add_test(
    NAME ir_cfg_dot_cli_smoke
    COMMAND
        ${CMAKE_COMMAND} -E env
            HOME=/tmp
            XDG_CACHE_HOME=/tmp/.cache
            BALTAM_FRONTEND=console
            LD_LIBRARY_PATH=/opt/Baltamatica/lib
            $<TARGET_FILE:ir_cfg_dot>
            "${CMAKE_SOURCE_DIR}/test/m/test3/test3_3.m"
            -o
            "${IR_CFG_DOT_CLI_SMOKE_DOT}"
            --svg
            "${IR_CFG_DOT_CLI_SMOKE_SVG}"
            --png
            "${IR_CFG_DOT_CLI_SMOKE_PNG}"
)
set_tests_properties(ir_cfg_dot_cli_smoke PROPERTIES
    LABELS "smoke;feature"
)

set(IR_PRINT_CLI_SMOKE_IR "${CMAKE_BINARY_DIR}/test/ir_print_cli/test4.ir")

add_test(
    NAME ir_print_cli_smoke
    COMMAND
        ${CMAKE_COMMAND} -E env
            HOME=/tmp
            XDG_CACHE_HOME=/tmp/.cache
            BALTAM_FRONTEND=console
            LD_LIBRARY_PATH=/opt/Baltamatica/lib
            $<TARGET_FILE:ir_print>
            "${CMAKE_SOURCE_DIR}/test/m/test4/test4.m"
            -o
            "${IR_PRINT_CLI_SMOKE_IR}"
            --no-line-numbers
)
set_tests_properties(ir_print_cli_smoke PROPERTIES
    LABELS "smoke;feature"
)

set(CFG_DOT_CASES
    test0/test0
    test0/test0_1
    test1/test1
    test1/test1_1
    test2/test2
    test2/test2_1
    test2/test2_2
    test2/test2_3
    test3/test3
    test3/test3_1
    test3/test3_2
    test3/test3_3
    test4/test4
    test4/test4_1
    test4/test4_2
    test4/test4_3
)

set(CFG_DOT_OUTPUTS)
foreach(case_name IN LISTS CFG_DOT_CASES)
    set(input_mfile "${CMAKE_SOURCE_DIR}/test/m/${case_name}.m")
    set(output_dot "${CMAKE_BINARY_DIR}/test/cfg_dot/${case_name}.dot")
    get_filename_component(output_dot_dir "${output_dot}" DIRECTORY)
    list(APPEND CFG_DOT_OUTPUTS "${output_dot}")

    add_custom_command(
        OUTPUT "${output_dot}"
        COMMAND ${CMAKE_COMMAND} -E make_directory "${output_dot_dir}"
        COMMAND ${CMAKE_COMMAND} -E env
            HOME=/tmp
            XDG_CACHE_HOME=/tmp/.cache
            BALTAM_FRONTEND=console
            LD_LIBRARY_PATH=/opt/Baltamatica/lib
            $<TARGET_FILE:ir_cfg_dot>
            "${input_mfile}"
            -o
            "${output_dot}"
        DEPENDS ir_cfg_dot "${input_mfile}"
        VERBATIM
    )
endforeach()

add_custom_target(generate_cfg_dot
    DEPENDS ${CFG_DOT_OUTPUTS}
)

add_executable(value_table_smoke
    feature/value_table_smoke.cpp
)

target_include_directories(value_table_smoke
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(value_table_smoke
    PRIVATE
        BALTAM_IR
)

add_test(
    NAME value_table_smoke
    COMMAND value_table_smoke
)
set_smoke_labels(value_table_smoke smoke feature)
