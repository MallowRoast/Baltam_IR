if(BALTAM_BUILD_UTILS)
    add_test(NAME baltam_ir_relative_path COMMAND baltam_ir
        syntax/parameter_slot.m)
    set_tests_properties(baltam_ir_relative_path PROPERTIES
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
        PASS_REGULAR_EXPRESSION "define @parameter_slot")
    add_test(NAME baltam_ir_absolute_path COMMAND baltam_ir
        ${CMAKE_CURRENT_SOURCE_DIR}/syntax/constant.m)
    set_tests_properties(baltam_ir_absolute_path PROPERTIES
        PASS_REGULAR_EXPRESSION "define @constant")
    add_test(NAME baltam_ir_multi_function COMMAND ${CMAKE_COMMAND}
        -DBALTAM_IR=$<TARGET_FILE:baltam_ir>
        -DM_FILE=${CMAKE_CURRENT_SOURCE_DIR}/syntax/multi_function.m
        -P ${PROJECT_SOURCE_DIR}/utils/check_multi_function.cmake)
    add_test(NAME baltam_ir_script_with_function COMMAND baltam_ir
        ${CMAKE_CURRENT_SOURCE_DIR}/syntax/script_with_function.m)
    set_tests_properties(baltam_ir_script_with_function PROPERTIES
        PASS_REGULAR_EXPRESSION "define @twice")
    add_test(NAME baltam_ir_output_file COMMAND ${CMAKE_COMMAND}
        -DBALTAM_IR=$<TARGET_FILE:baltam_ir>
        -DM_FILE=${CMAKE_CURRENT_SOURCE_DIR}/syntax/constant.m
        -DOUTPUT_FILE=${CMAKE_CURRENT_BINARY_DIR}/constant.ir
        -DINPUT_COPY=${CMAKE_CURRENT_BINARY_DIR}/constant_copy.m
        -P ${CMAKE_CURRENT_SOURCE_DIR}/check_output_file.cmake)
endif()

if(BALTAM_ENABLE_AST_TEST)
    add_executable(ast_ir_lowering_test ast_ir_lowering_test.cpp)
    target_link_libraries(ast_ir_lowering_test PRIVATE baltam_ir_core)
    add_executable(ir_cache_integration_test ir_cache_integration_test.cpp)
    target_link_libraries(ir_cache_integration_test PRIVATE baltam_ir_core)
    add_test(NAME ir_cache_integration_test COMMAND ir_cache_integration_test
        ${CMAKE_CURRENT_SOURCE_DIR}/syntax/script_only.m
        ${CMAKE_CURRENT_SOURCE_DIR}/syntax/script_with_function.m)
    foreach(syntax
        constant concat cell colon unary binary_arithmetic binary_compare_logic
        storage_decl if_else switch_case for_loop while_loop parameter_slot
        brace_index paren_index call_statement dot_access short_circuit
        script_only script_with_function nested_function)
        add_test(NAME ast_${syntax}_ir_test COMMAND ast_ir_lowering_test
            ${CMAKE_CURRENT_SOURCE_DIR}/syntax/${syntax}.m ${syntax})
    endforeach()
endif()
