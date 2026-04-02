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
