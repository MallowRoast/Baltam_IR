#include "ir/ir_cache.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace {

void require(bool condition, const char* message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_file(const std::filesystem::path& path, std::string_view bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    require(output.good(), "测试源文件写入失败");
}

std::string read_file(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    require(static_cast<bool>(input), "测试源文件读取失败");
    return {std::istreambuf_iterator<char>{input}, {}};
}

struct TempDirectory {
    std::filesystem::path path;

    ~TempDirectory() {
        std::error_code error;
        std::filesystem::remove_all(path, error);
    }
};

} // 匿名命名空间

int main() {
    using namespace baltam;
    namespace fs = std::filesystem;

    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    TempDirectory temp{fs::temp_directory_path() /
                       ("baltam_ir_cache_smoke_" + std::to_string(unique))};
    require(fs::create_directory(temp.path), "测试临时目录创建失败");
    const auto file = temp.path / "sample.m";
    write_file(file, "x = 1;");

    auto& cache = IRCache::instance();
    auto& worlds = WorldCounter::instance();
    int build_count = 0;
    std::string built_bytes;
    const auto load = [&]() {
        auto entry = cache.find(file);
        if (entry && entry->valid()) {
            return entry;
        }
        built_bytes = read_file(file);
        ++build_count;
        auto module = std::make_shared<IRModule>(
            SourceType::File, ModuleType::Script, file);
        module->set_command_unit(std::make_unique<CommandUnit>());
        return cache.register_module(std::move(module));
    };

    auto first = load();
    require(build_count == 1 && built_bytes == "x = 1;" &&
                cache.find(file) == first && first->module->command_unit(),
            "首次读取未构建并注册模块");
    require(!first->invalidated && first->checked_world == worlds.current(),
            "新条目的有效状态或世界计数不正确");
    require(load() == first && build_count == 1,
            "同一世界内未快速命中缓存");

    const auto next_world = worlds.advance();
    require(load() == first && first->checked_world == next_world &&
                build_count == 1,
            "世界变化但文件未变化时未复用模块");

    const auto first_mtime = fs::last_write_time(file);
    auto first_module = first->module;
    write_file(file, "x = 2;");
    fs::last_write_time(file, first_mtime + std::chrono::seconds(2));
    (void)worlds.advance();
    auto second = load();
    require(second && second->module != first_module && build_count == 2 &&
                built_bytes == "x = 2;",
            "文件修改后未重新构建模块");
    require(first_module->command_unit() != nullptr,
            "缓存替换后仍被持有的旧模块已失效");

    auto second_module = second->module;
    cache.remove(file);
    require(cache.find(file) == nullptr && second_module->command_unit(),
            "移除缓存后旧模块生命周期不正确");
    auto third = load();
    require(third && third->module != second_module && build_count == 3,
            "移除缓存后未重新注册模块");
    cache.remove(file);
}
