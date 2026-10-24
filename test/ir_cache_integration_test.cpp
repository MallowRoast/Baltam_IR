#include "ir/ir_builder.h"
#include "ir/ir_cache.h"
#include "ir/world_counter.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

using namespace baltam;
namespace fs = std::filesystem;

int main(int argc, char* argv[]) {
    if (argc != 3) return 2;
    try {
        const auto first = IRBuilder::build_file(argv[1]);
        const auto second = IRBuilder::build_file(argv[2]);
        if (first == second || IRBuilder::build_file(argv[1]) != first ||
            IRBuilder::build_file(argv[2]) != second ||
            IRCache::instance().find(first->source_path().value())->module != first ||
            IRCache::instance().find(second->source_path().value())->module != second)
            return 1;

        const auto temp_dir = fs::temp_directory_path() /
            ("baltam_ir_cache_" + std::to_string(
                std::chrono::steady_clock::now().time_since_epoch().count()));
        fs::create_directory(temp_dir);
        const auto temp_file = temp_dir / "script_only.m";
        fs::copy_file(argv[1], temp_file);
        const auto old = IRBuilder::build_file(temp_file);
        fs::last_write_time(temp_file,
            fs::last_write_time(temp_file) + std::chrono::seconds(2));
        (void)WorldCounter::instance().advance();
        const auto updated = IRBuilder::build_file(temp_file);
        const bool valid = updated != old &&
            IRBuilder::build_file(temp_file) == updated &&
            IRCache::instance().find(temp_file)->module == updated &&
            old->command_unit() && updated->command_unit() &&
            IRBuilder::build_file(argv[1]) == first;
        IRCache::instance().remove(temp_file);
        fs::remove_all(temp_dir);
        return valid ? 0 : 1;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
