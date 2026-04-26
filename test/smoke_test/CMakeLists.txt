function(add_ir_smoke_test target source macro_name mfile_relpath)
    add_executable(${target}
        ${source}
    )

    target_compile_definitions(${target}
        PRIVATE
            ${macro_name}="${CMAKE_SOURCE_DIR}/${mfile_relpath}"
    )

    # 这些 smoke test 目前依赖源码树内的 core/logger so，以及本机 `/opt/Baltamatica/lib`
    # 里的运行库。这是为当前开发环境排查 parser/lowering/print 链路加入的本地配置，不可直接视为
    # 可迁移默认值；后续若切到安装态运行，需要改成更稳定的依赖发现与部署方案。
    set_target_properties(${target} PROPERTIES
        BUILD_RPATH "${CMAKE_SOURCE_DIR}/deps/core/lib;${CMAKE_SOURCE_DIR}/deps/logger/lib;/opt/Baltamatica/lib"
    )

    target_link_libraries(${target}
        PRIVATE
            BALTAM_IR
    )

    add_test(
        NAME ${target}
        COMMAND ${target}
    )

    # 这里额外注入 `HOME=/tmp`、`XDG_CACHE_HOME=/tmp/.cache` 和 `LD_LIBRARY_PATH`，
    # 是为了规避当前本地运行时的初始化/找库问题；这些环境变量带有明显的机器与目录假设。
    set_tests_properties(${target} PROPERTIES
        ENVIRONMENT
            "HOME=/tmp;XDG_CACHE_HOME=/tmp/.cache;BALTAM_FRONTEND=console;LD_LIBRARY_PATH=${CMAKE_SOURCE_DIR}/deps/core/lib:${CMAKE_SOURCE_DIR}/deps/logger/lib:/opt/Baltamatica/lib"
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
    test/m/test0_1/test0_1.m
)
