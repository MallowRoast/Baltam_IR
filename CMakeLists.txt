cmake_minimum_required(VERSION 3.16)

project(Baltam_IR LANGUAGES CXX)

include(CTest)

add_library(BALTAM_IR STATIC
    src/ir/ir_builder.cpp
    src/ir/ir_lowering.cpp
    src/ir/ir_print.cpp
    src/ir/ir_type.cpp
    src/ir/ir_verify.cpp
    src/pass/cfg_simplification_pass.cpp
    src/pass/constant_deduplication_pass.cpp
    src/pass/constant_folding_pass.cpp
    src/pass/dead_code_elimination_pass.cpp
    src/pass/dead_branch_elimination_pass.cpp
    src/pass/load_forwarding_pass.cpp
    src/pass/ir_pass_manager.cpp
    src/pass/unreachable_block_elimination_pass.cpp
    src/runtime/code_object.cpp
    src/runtime/frame.cpp
    src/runtime/ir_executor.cpp
    src/runtime/interpreter_context.cpp
    src/runtime/lookup.cpp
)

target_include_directories(BALTAM_IR
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/core/include
        ${CMAKE_CURRENT_SOURCE_DIR}/deps/core/include/bt_ast_interface
)

target_compile_features(BALTAM_IR PUBLIC cxx_std_17)

set(BALTAM_SYSTEM_LIB_DIR "/opt/Baltamatica/lib" CACHE PATH
    "Directory containing the installed Baltamatica runtime libraries")
set(BALTAM_DEPS_CORE_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/core/lib" CACHE PATH
    "Fallback directory containing vendored core runtime libraries")
set(BALTAM_DEPS_LOGGER_LIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/logger/lib" CACHE PATH
    "Fallback directory containing vendored logger runtime libraries")

function(resolve_baltam_library output_var library_name fallback_dir)
    set(primary_lib "${BALTAM_SYSTEM_LIB_DIR}/lib${library_name}.so")
    set(fallback_lib "${fallback_dir}/lib${library_name}.so")

    if (EXISTS "${primary_lib}")
        set(${output_var} "${primary_lib}" PARENT_SCOPE)
    elseif (EXISTS "${fallback_lib}")
        message(WARNING
            "Using vendored fallback library for ${library_name}: ${fallback_lib}")
        set(${output_var} "${fallback_lib}" PARENT_SCOPE)
    else()
        message(FATAL_ERROR
            "Could not find lib${library_name}.so in ${BALTAM_SYSTEM_LIB_DIR}"
            " or ${fallback_dir}")
    endif()
endfunction()

resolve_baltam_library(BALTAM_BT_CORE_LIB bt_core "${BALTAM_DEPS_CORE_LIB_DIR}")
resolve_baltam_library(BALTAM_BT_WORKER_LIB bt_worker "${BALTAM_DEPS_CORE_LIB_DIR}")
resolve_baltam_library(BALTAM_BT_AST_LIB bt_ast "${BALTAM_DEPS_CORE_LIB_DIR}")
resolve_baltam_library(BALTAM_BT_AST_BASE_LIB bt_ast_base "${BALTAM_DEPS_CORE_LIB_DIR}")
resolve_baltam_library(BALTAM_BT_LOGGER_LIB bt_logger "${BALTAM_DEPS_LOGGER_LIB_DIR}")

set(BALTAM_RUNTIME_RPATH "${BALTAM_SYSTEM_LIB_DIR}")
foreach(runtime_lib
        ${BALTAM_BT_CORE_LIB}
        ${BALTAM_BT_WORKER_LIB}
        ${BALTAM_BT_AST_LIB}
        ${BALTAM_BT_AST_BASE_LIB}
        ${BALTAM_BT_LOGGER_LIB})
    get_filename_component(runtime_lib_dir "${runtime_lib}" DIRECTORY)
    list(APPEND BALTAM_RUNTIME_RPATH "${runtime_lib_dir}")
endforeach()
list(REMOVE_DUPLICATES BALTAM_RUNTIME_RPATH)
list(JOIN BALTAM_RUNTIME_RPATH ":" BALTAM_RUNTIME_LD_LIBRARY_PATH)

target_link_libraries(BALTAM_IR
    PUBLIC
        ${BALTAM_BT_CORE_LIB}
        ${BALTAM_BT_WORKER_LIB}
        ${BALTAM_BT_AST_LIB}
        ${BALTAM_BT_AST_BASE_LIB}
        ${BALTAM_BT_LOGGER_LIB}
)

add_executable(ir_cfg_dot
    tools/cfg_dot.cpp
    tools/cfg_dot_main.cpp
)

target_include_directories(ir_cfg_dot
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

set_target_properties(ir_cfg_dot PROPERTIES
    BUILD_RPATH "${BALTAM_RUNTIME_RPATH}"
)

target_link_libraries(ir_cfg_dot
    PRIVATE
        BALTAM_IR
)

add_executable(ir_print
    tools/ir_print_main.cpp
)

set_target_properties(ir_print PROPERTIES
    BUILD_RPATH "${BALTAM_RUNTIME_RPATH}"
)

target_link_libraries(ir_print
    PRIVATE
        BALTAM_IR
)

set(CMAKE_BUILD_RPATH
    "${BALTAM_RUNTIME_RPATH}")

if (BUILD_TESTING)
    add_subdirectory(test)
endif()
