#pragma once

#include "ir/ir_inst.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace baltam {

// 每个模块使用一个编码器；元数据中的路径相对于资源目录解析。
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
