#pragma once

#include "ir/ir_unit.h"
#include "ir/ir_constant.h"

#include <filesystem>
#include <iosfwd>
#include <memory>
#include <string>
#include <string_view>

namespace baltam {

// 统一输出模块，并管理本次打印的常量标签和 MAT 资源。
// 每次 print 都重置编码状态；同一对象可顺序打印多个模块。
class IRPrinter {
public:
    explicit IRPrinter(std::filesystem::path resource_directory = {},
                       std::filesystem::path constant_directory = "constants");

    [[nodiscard]] std::string print(const IRModule& module);

private:
    void print_slots(std::ostream& output, const IRUnit& unit) const;

    void print_blocks(std::ostream& output, const IRStageVersion& version);

    void print_function(std::ostream& output, const FunctionUnit& function,
                        const std::string& path);

    std::filesystem::path resource_directory_;
    std::filesystem::path constant_directory_;
    ConstantEncoder constants_;
};

// 仅当常量引用相对 MAT 路径时，才需要提供资源目录。
[[nodiscard]] std::unique_ptr<IRModule>
read_ir(std::string_view text, const std::filesystem::path& resource_directory = {});

[[nodiscard]] std::unique_ptr<IRModule> read_ir_file(const std::filesystem::path& path);

// 配套 MAT 文件保存到 IR 文件同目录下的 <文件名>.constants/ 中。
void write_ir_file(const IRModule& module, const std::filesystem::path& path);

} // baltam 命名空间
