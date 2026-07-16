#pragma once

#include "ir/ir_units.h"

#include "ba_obj/ba_obj.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace baltam {

struct RuntimeFrame;

struct PersistentTable {
    std::unordered_map<SlotId, ba_obj_ptr> values;

    [[nodiscard]] ba_obj_ptr find(SlotId slot) const;
    void clear(SlotId slot);
};

class CodeObject final {
public:
    std::shared_ptr<IRModule> ir_owner;
    CodeUnit* unit = nullptr;

    PersistentTable persistent;

    bool invalidated = false;
    std::uint64_t revision = 0;
};

struct CodeCacheKey {
    NormalizedPath source_file;
    InternedString unit_name;

    [[nodiscard]] friend bool operator==(
        const CodeCacheKey& lhs,
        const CodeCacheKey& rhs);
};

struct CodeCacheKeyHash {
    std::size_t operator()(const CodeCacheKey& key) const noexcept;
};

struct AnonymousCodeKey {
    const IRModule* module = nullptr;
    AnonymousFunctionId function_id = InvalidAnonymousFunctionId;

    [[nodiscard]] friend bool operator==(
        AnonymousCodeKey lhs,
        AnonymousCodeKey rhs) noexcept;
};

struct AnonymousCodeKeyHash {
    std::size_t operator()(AnonymousCodeKey key) const noexcept;
};

class CodeObjectCache final {
public:
    [[nodiscard]] std::shared_ptr<CodeObject> find(const CodeCacheKey& key) const;

    void insert(CodeCacheKey key, std::shared_ptr<CodeObject> code);
    void invalidate(const CodeCacheKey& key);
    void invalidate_source(const NormalizedPath& source_file);
    void invalidate_all();
    void collect_retired(const RuntimeFrame* current_frame);

private:
    std::unordered_map<CodeCacheKey, std::shared_ptr<CodeObject>, CodeCacheKeyHash> entries_;
    std::vector<std::shared_ptr<CodeObject>> retired_;
};

class AnonymousCodeTable final {
public:
    [[nodiscard]] std::shared_ptr<CodeObject> find(AnonymousCodeKey key) const;

    void insert(AnonymousCodeKey key, std::shared_ptr<CodeObject> code);
    void invalidate_module(const IRModule* module);
    void invalidate_all();
    void collect_retired(const RuntimeFrame* current_frame);

private:
    std::unordered_map<AnonymousCodeKey, std::shared_ptr<CodeObject>, AnonymousCodeKeyHash>
        entries_;
    std::vector<std::shared_ptr<CodeObject>> retired_;
};

} // namespace baltam
