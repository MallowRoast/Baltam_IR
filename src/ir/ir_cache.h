#pragma once

#include "ir/ir_unit.h"
#include "ir/world_counter.h"

#include <filesystem>
#include <memory>
#include <unordered_map>

namespace baltam {

// 一个文件源码版本的缓存记录；已检查的世界计数仅用于控制文件检查频率。
struct CacheEntry {
    std::shared_ptr<IRModule> module;
    std::filesystem::file_time_type mtime;
    WorldId world = 0;
    bool valid = true;

    // 显式失效或修改时间变化时返回 false；每个世界最多检查一次文件。
    [[nodiscard]] bool is_valid();
};

// 仅管理文件来源的缓存条目。第一阶段由单线程调用。
class IRCache final {
public:
    [[nodiscard]] static IRCache& instance() noexcept;

    IRCache(const IRCache&) = delete;
    IRCache& operator=(const IRCache&) = delete;
    IRCache(IRCache&&) = delete;
    IRCache& operator=(IRCache&&) = delete;

    // 返回非拥有指针；替换或移除对应条目后，该指针失效。
    [[nodiscard]] CacheEntry* find(
        const std::filesystem::path& source_path) const;

    // 调用处完成构建后注册，并记录当前文件修改时间和世界计数。
    // 返回非拥有指针；需要跨越缓存替换使用模块时，应复制条目的 module。
    [[nodiscard]] CacheEntry* register_module(
        std::shared_ptr<IRModule> module);

    void remove(const std::filesystem::path& source_path);

private:
    IRCache() = default;

    std::unordered_map<std::filesystem::path, std::unique_ptr<CacheEntry>> entries_;
};

} // baltam 命名空间
