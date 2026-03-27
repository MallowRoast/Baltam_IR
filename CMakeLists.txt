cmake_minimum_required(VERSION 3.11)

project(TEST_KERNEL)

# cmake_policy(SET CMP0167 OLD)

add_executable(main
    src/main.cpp
    src/ir/ir.cpp
)
target_compile_definitions(main PRIVATE BALTAM_IR_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
target_include_directories(main PRIVATE src)

# boost
find_package(Boost REQUIRED)

find_package(baltam_core 3.8.4 REQUIRED COMPONENTS dev)
find_package(baltam_kernel 3.10.2 REQUIRED)

target_link_libraries(main PRIVATE baltam_kernel::bt_ast)
