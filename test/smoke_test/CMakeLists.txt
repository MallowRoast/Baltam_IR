add_executable(test0_smoke
    test0_smoke.cpp
)

target_compile_definitions(test0_smoke
    PRIVATE
        TEST0_MFILE_PATH="${CMAKE_SOURCE_DIR}/test/m/test0/test0.m"
)

# `test0_smoke` 目前依赖源码树内的 core/logger so，以及本机 `/opt/Baltamatica/lib`
# 里的运行库。这是为当前开发环境排查 parser/lowering/print 链路加入的本地配置，不可直接视为
# 可迁移默认值；后续若切到安装态运行，需要改成更稳定的依赖发现与部署方案。
set_target_properties(test0_smoke PROPERTIES
    BUILD_RPATH "${CMAKE_SOURCE_DIR}/deps/core/lib;${CMAKE_SOURCE_DIR}/deps/logger/lib;/opt/Baltamatica/lib"
)

target_link_libraries(test0_smoke
    PRIVATE
        BALTAM_IR
)

add_test(
    NAME test0_smoke
    COMMAND test0_smoke
)

# 这里额外注入 `HOME=/tmp`、`XDG_CACHE_HOME=/tmp/.cache` 和 `LD_LIBRARY_PATH`，
# 是为了规避当前本地运行时的初始化/找库问题；这些环境变量带有明显的机器与目录假设。
set_tests_properties(test0_smoke PROPERTIES
    ENVIRONMENT
        "HOME=/tmp;XDG_CACHE_HOME=/tmp/.cache;BALTAM_FRONTEND=console;LD_LIBRARY_PATH=${CMAKE_SOURCE_DIR}/deps/core/lib:${CMAKE_SOURCE_DIR}/deps/logger/lib:/opt/Baltamatica/lib"
)
