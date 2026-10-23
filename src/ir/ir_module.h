#pragma once

#include "ir/ir_unit.h"

#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace baltam {

enum class SourceType : std::uint8_t { File, Command, Memory };
enum class ModuleType : std::uint8_t { Script, FunctionFile, ClassFile, Command };
using IndexedFunction = std::pair<std::string, std::unique_ptr<FunctionUnit>>;

// 一个模块对应一次源码输入生成的 IR；所有权树发布后不再修改。
class IRModule final {
public:
    IRModule(SourceType source_type, ModuleType module_type,
             std::optional<std::filesystem::path> source_path);

    IRModule(const IRModule&) = delete;
    IRModule& operator=(const IRModule&) = delete;
    IRModule(IRModule&&) = delete;
    IRModule& operator=(IRModule&&) = delete;

    [[nodiscard]] SourceType source_type() const noexcept { return source_type_; }
    [[nodiscard]] ModuleType module_type() const noexcept { return module_type_; }
    [[nodiscard]] const std::optional<std::filesystem::path>& source_path() const noexcept {
        return source_path_;
    }
    [[nodiscard]] const CommandUnit* command_unit() const noexcept {
        return command_unit_.get();
    }
    [[nodiscard]] const FunctionUnit* entry_function() const noexcept {
        return module_type_ == ModuleType::FunctionFile && !functions_.empty()
                   ? functions_.front().get() : nullptr;
    }
    [[nodiscard]] const std::vector<std::unique_ptr<FunctionUnit>>&
    functions() const noexcept {
        return functions_;
    }
    // 只查找模块直接拥有的函数；不遍历嵌套函数，也不执行 M 语言的可见性检查。
    [[nodiscard]] const FunctionUnit* find_function(std::string_view key) const;

    void set_command_unit(std::unique_ptr<CommandUnit>&& unit);
    // 按传入顺序保存函数；函数文件的主函数由调用处放在首位。
    void set_functions(std::vector<IndexedFunction>&& functions);
private:
    SourceType source_type_;
    ModuleType module_type_;
    std::optional<std::filesystem::path> source_path_;

    std::unique_ptr<CommandUnit> command_unit_;
    std::vector<std::unique_ptr<FunctionUnit>> functions_;
    std::map<std::string, FunctionUnit*, std::less<>> function_index_;
};

} // baltam 命名空间
