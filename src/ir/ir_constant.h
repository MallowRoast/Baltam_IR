#pragma once

#include "ir/ir_inst.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace baltam {

// 类型、完整形状和精确数据组成的判等键；不读取或写入 MAT 文件。
[[nodiscard]] std::string constant_key(const ConstInst& instruction);

// 删除无使用的常量定义；调用方保证当前版本不被回退映射引用。
bool remove_unused_constants(IRStageVersion& version);

// 对具体常量调用 as_bool；转换抛错或输入不是具体常量时不折叠。
[[nodiscard]] const_ba_obj_ptr try_fold_logical_scalar(const IRStageVersion& version,
                                                       ValueId input);

// 所有元素均为具体 ConstInst 时构造只读元胞对象，否则返回空指针。
[[nodiscard]] const_ba_obj_ptr make_cell_constant(const IRStageVersion& version,
                                                  const std::vector<ValueId>& items,
                                                  std::size_t rows, std::size_t cols);

// 每个模块使用一个编码器；输出元数据使用 MAT 文件的绝对路径，文件名对应常量标签。
class ConstantEncoder {
public:
    explicit ConstantEncoder(std::filesystem::path resource_directory,
                             std::filesystem::path constant_directory = "constants");

    [[nodiscard]] std::string encode(const ConstInst& instruction);

    [[nodiscard]] std::string metadata() const;

private:
    std::filesystem::path directory_;
    std::filesystem::path constant_directory_;
    std::unordered_map<std::string, std::size_t> labels_;
    std::vector<std::string> definitions_;
};

// 传入模块的全部常量指令，以便校验共享标签的类型和形状。
// 每个标签只加载一次，解码结果共享对应的只读对象。
class ConstantDecoder {
public:
    ConstantDecoder(std::string_view metadata, const std::vector<std::string>& instructions,
                    std::filesystem::path resource_directory);

    [[nodiscard]] std::unique_ptr<ConstInst> decode(std::string_view instruction) const;

private:
    struct Entry {
        std::string descriptor;
        const_ba_obj_ptr value;
    };

    std::unordered_map<std::string, Entry> entries_;
};

} // baltam 命名空间
