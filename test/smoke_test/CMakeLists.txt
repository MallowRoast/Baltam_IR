function(add_ir_smoke_test target source macro_name mfile_relpath)
    add_executable(${target}
        ${source}
    )

    target_compile_definitions(${target}
        PRIVATE
            ${macro_name}="${CMAKE_SOURCE_DIR}/${mfile_relpath}"
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

add_ir_smoke_test(
    test0_smoke
    test0_smoke.cpp
    TEST0_MFILE_PATH
    test/m/test0/test0.m
)

add_ir_smoke_test(
    test0_1_smoke
    test0_1_smoke.cpp
    TEST0_1_MFILE_PATH
    test/m/test0/test0_1.m
)

add_ir_smoke_test(
    test1_smoke
    test1_smoke.cpp
    TEST1_MFILE_PATH
    test/m/test1/test1.m
)

add_ir_smoke_test(
    test1_1_smoke
    test1_1_smoke.cpp
    TEST1_1_MFILE_PATH
    test/m/test1/test1_1.m
)

add_executable(ir_type_smoke
    ir_type_smoke.cpp
)

target_link_libraries(ir_type_smoke
    PRIVATE
        BALTAM_IR
)

add_test(
    NAME ir_type_smoke
    COMMAND ir_type_smoke
)

add_executable(value_table_smoke
    value_table_smoke.cpp
)

target_link_libraries(value_table_smoke
    PRIVATE
        BALTAM_IR
)

add_test(
    NAME value_table_smoke
    COMMAND value_table_smoke
)
