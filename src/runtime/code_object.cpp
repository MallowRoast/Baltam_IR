#include "runtime/code_object.h"

#include "runtime/frame.h"

#include <algorithm>
#include <utility>

namespace baltam {
namespace {

template <typename Key, typename Map>
void retire_entry(
    Map& entries,
    std::vector<std::shared_ptr<CodeObject>>& retired,
    const Key& key) {
    auto it = entries.find(key);
    if (it == entries.end()) {
        return;
    }

    if (it->second != nullptr) {
        it->second->invalidated = true;
        retired.push_back(std::move(it->second));
    }
    entries.erase(it);
}

void retire_code(
    std::shared_ptr<CodeObject> code,
    std::vector<std::shared_ptr<CodeObject>>& retired) {
    if (code == nullptr) {
        return;
    }

    code->invalidated = true;
    retired.push_back(std::move(code));
}

void collect_retired_impl(
    std::vector<std::shared_ptr<CodeObject>>& retired,
    const RuntimeFrame* current_frame) {
    retired.erase(
        std::remove_if(
            retired.begin(),
            retired.end(),
            [current_frame](const std::shared_ptr<CodeObject>& code) {
                return code == nullptr || !frame_chain_contains(current_frame, code.get());
            }),
        retired.end());
}

} // namespace

ba_obj_ptr PersistentTable::find(SlotId slot) const {
    const auto it = values.find(slot);
    return it != values.end() ? it->second : ba_obj_ptr{};
}

void PersistentTable::clear(SlotId slot) {
    const auto it = values.find(slot);
    if (it != values.end()) {
        it->second.reset();
    }
}

bool CodeObject::executable() const noexcept {
    return !invalidated && ir_owner != nullptr && unit != nullptr;
}

bool operator==(const CodeCacheKey& lhs, const CodeCacheKey& rhs) {
    return lhs.source_file == rhs.source_file && lhs.unit_name == rhs.unit_name;
}

std::size_t CodeCacheKeyHash::operator()(const CodeCacheKey& key) const noexcept {
    const std::size_t path_hash = std::hash<NormalizedPath>{}(key.source_file);
    const std::size_t unit_hash = std::hash<InternedString>{}(key.unit_name);
    return path_hash ^ (unit_hash << 1U);
}

bool operator==(AnonymousCodeKey lhs, AnonymousCodeKey rhs) noexcept {
    return lhs.module == rhs.module && lhs.function_id == rhs.function_id;
}

std::size_t AnonymousCodeKeyHash::operator()(AnonymousCodeKey key) const noexcept {
    const std::size_t module_hash = std::hash<const IRModule*>{}(key.module);
    const std::size_t function_hash = std::hash<AnonymousFunctionId>{}(key.function_id);
    return module_hash ^ (function_hash << 1U);
}

std::shared_ptr<CodeObject> CodeObjectCache::find(const CodeCacheKey& key) const {
    const auto it = entries_.find(key);
    if (it == entries_.end() || it->second == nullptr || it->second->invalidated) {
        return nullptr;
    }
    return it->second;
}

void CodeObjectCache::insert(CodeCacheKey key, std::shared_ptr<CodeObject> code) {
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        retire_code(std::move(it->second), retired_);
        it->second = std::move(code);
    } else {
        it = entries_.emplace(std::move(key), std::move(code)).first;
    }

    if (it->second != nullptr) {
        it->second->invalidated = false;
    }
}

void CodeObjectCache::invalidate(const CodeCacheKey& key) {
    retire_entry(entries_, retired_, key);
}

void CodeObjectCache::invalidate_source(const NormalizedPath& source_file) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->first.source_file == source_file) {
            retire_code(std::move(it->second), retired_);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

void CodeObjectCache::invalidate_all() {
    for (auto& entry : entries_) {
        retire_code(std::move(entry.second), retired_);
    }
    entries_.clear();
}

void CodeObjectCache::collect_retired(const RuntimeFrame* current_frame) {
    collect_retired_impl(retired_, current_frame);
}

std::shared_ptr<CodeObject> AnonymousCodeTable::find(AnonymousCodeKey key) const {
    const auto it = entries_.find(key);
    if (it == entries_.end() || it->second == nullptr || it->second->invalidated) {
        return nullptr;
    }
    return it->second;
}

void AnonymousCodeTable::insert(AnonymousCodeKey key, std::shared_ptr<CodeObject> code) {
    auto it = entries_.find(key);
    if (it != entries_.end()) {
        retire_code(std::move(it->second), retired_);
        it->second = std::move(code);
    } else {
        it = entries_.emplace(key, std::move(code)).first;
    }

    if (it->second != nullptr) {
        it->second->invalidated = false;
    }
}

void AnonymousCodeTable::invalidate_module(const IRModule* module) {
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->first.module == module) {
            retire_code(std::move(it->second), retired_);
            it = entries_.erase(it);
        } else {
            ++it;
        }
    }
}

void AnonymousCodeTable::invalidate_all() {
    for (auto& entry : entries_) {
        retire_code(std::move(entry.second), retired_);
    }
    entries_.clear();
}

void AnonymousCodeTable::collect_retired(const RuntimeFrame* current_frame) {
    collect_retired_impl(retired_, current_frame);
}

} // namespace baltam
