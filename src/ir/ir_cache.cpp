#include "ir/ir_cache.h"

#include <stdexcept>
#include <system_error>

namespace baltam {
namespace {

std::filesystem::path normalized_m_file_path(
    const std::filesystem::path& source_path) {
    if (source_path.empty() || !source_path.is_absolute() ||
        source_path.extension() != ".m") {
        throw std::invalid_argument("缓存路径必须是绝对的 .m 文件路径");
    }
    return source_path.lexically_normal();
}

std::filesystem::file_time_type read_mtime(
    const std::filesystem::path& source_path) {
    std::error_code error;
    const auto mtime = std::filesystem::last_write_time(source_path, error);
    if (error) {
        throw std::runtime_error("无法获取源文件修改时间：" + source_path.string());
    }
    return mtime;
}

} // 匿名命名空间

bool CacheEntry::is_valid() {
    if (!valid) {
        return false;
    }
    if (!module || module->source_type() != SourceType::File ||
        !module->source_path()) {
        valid = false;
        return false;
    }
    const auto world = WorldCounter::instance().current();
    if (this->world == world) {
        return true;
    }
    std::error_code error;
    const auto current_mtime = std::filesystem::last_write_time(
        *module->source_path(), error);
    if (error || current_mtime != mtime) {
        valid = false;
        return false;
    }
    this->world = world;
    return true;
}

IRCache& IRCache::instance() noexcept {
    static IRCache cache;
    return cache;
}

CacheEntry* IRCache::find(
    const std::filesystem::path& source_path) const {
    const auto path = normalized_m_file_path(source_path);
    const auto it = entries_.find(path);
    return it == entries_.end() ? nullptr : it->second.get();
}

CacheEntry* IRCache::register_module(
    std::shared_ptr<IRModule> module) {
    if (!module || module->source_type() != SourceType::File ||
        !module->source_path()) {
        throw std::invalid_argument("只能注册文件来源的模块");
    }
    const auto path = normalized_m_file_path(*module->source_path());
    const auto mtime = read_mtime(path);
    auto entry = std::make_unique<CacheEntry>(CacheEntry{
        std::move(module), mtime, WorldCounter::instance().current(), true});
    auto* registered = entry.get();
    entries_.insert_or_assign(path, std::move(entry));
    return registered;
}

void IRCache::remove(const std::filesystem::path& source_path) {
    const auto path = normalized_m_file_path(source_path);
    entries_.erase(path);
}

} // baltam 命名空间
