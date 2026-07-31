#pragma once

#include "runtime/code_object.h"

#include "ba_obj/ba_obj.h"

#include <atomic>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <vector>

namespace baltam {

struct RuntimeFrame;

struct BaseWorkspace {
    std::unordered_map<InternedString, ba_obj_ptr> values;

    [[nodiscard]] ba_obj_ptr find(InternedString name) const;
    void clear(InternedString name);
};

struct GlobalRegistry {
    std::unordered_map<InternedString, ba_obj_ptr> values;

    [[nodiscard]] ba_obj_ptr find(InternedString name) const;
    void clear(InternedString name);
};

struct MFileCache {
    NormalizedPath path;
    std::uint64_t epoch = 0;
    std::filesystem::file_time_type mtime{};
    std::unique_ptr<MFileUnit> file;
};

class InterpreterContext final {
public:
    std::unordered_map<NormalizedPath, MFileCache> mfiles;
    std::unique_ptr<CommandUnit> command;
    std::vector<std::shared_ptr<AnonymousFunctionUnit>> anonymous_functions;

    BaseWorkspace base_workspace;
    GlobalRegistry globals;

    CodeObjectCache code_cache;
    AnonymousCodeTable anonymous_codes;

    RuntimeFrame* current_frame = nullptr;

    std::atomic_bool interrupt_requested{false};
};

} // namespace baltam
