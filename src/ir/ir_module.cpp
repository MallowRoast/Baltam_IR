#include "ir/ir_module.h"

#include <stdexcept>
#include <utility>

namespace baltam {
IRModule::IRModule(SourceType source_type, ModuleType module_type,
                   std::optional<std::filesystem::path> source_path)
    : source_type_(source_type),
      module_type_(module_type),
      source_path_(std::move(source_path)) {
    if (source_type_ == SourceType::File) {
        if (!source_path_ || source_path_->empty() || !source_path_->is_absolute()) {
            throw std::invalid_argument("文件类型的源码必须提供绝对路径");
        }
        source_path_ = source_path_->lexically_normal();
    } else if (source_path_) {
        throw std::invalid_argument("非文件类型的源码不能设置路径");
    }
    if ((source_type_ == SourceType::Command) != (module_type_ == ModuleType::Command)) {
        throw std::invalid_argument("命令输入的源码类型与模块类型必须对应");
    }
}

const FunctionUnit* IRModule::find_function(std::string_view key) const {
    const auto it = function_index_.find(key);
    return it == function_index_.end() ? nullptr : it->second;
}

void IRModule::set_command_unit(std::unique_ptr<CommandUnit>&& unit) {
    if (module_type_ != ModuleType::Script && module_type_ != ModuleType::Command) {
        throw std::logic_error("当前模块类型不能包含命令单元");
    }
    if (!unit) {
        throw std::invalid_argument("命令单元不能为空");
    }
    if (command_unit_) {
        throw std::logic_error("命令单元已存在");
    }
    command_unit_ = std::move(unit);
}

void IRModule::set_functions(std::vector<IndexedFunction>&& functions) {
    if (module_type_ == ModuleType::Command && !functions.empty()) {
        throw std::logic_error("命令模块不能包含函数");
    }
    if (!functions_.empty()) {
        throw std::logic_error("模块级函数已设置");
    }
    std::map<std::string, FunctionUnit*, std::less<>> new_index;
    for (const auto& item : functions) {
        if (item.first.empty() || !item.second || item.second->name().empty()) {
            throw std::invalid_argument("函数索引键、函数对象和函数名称均不能为空");
        }
        if (!new_index.emplace(item.first, item.second.get()).second) {
            throw std::invalid_argument("函数索引键重复");
        }
    }
    std::vector<std::unique_ptr<FunctionUnit>> new_functions;
    new_functions.reserve(functions.size());
    for (auto& item : functions) {
        new_functions.push_back(std::move(item.second));
    }
    functions_.swap(new_functions);
    function_index_.swap(new_index);
}

} // baltam 命名空间
