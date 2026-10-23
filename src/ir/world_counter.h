#pragma once

#include <cstdint>

namespace baltam {

using WorldId = std::uint64_t;

// 全局世界计数；第一阶段由单线程调用。
class WorldCounter final {
public:
    [[nodiscard]] static WorldCounter& instance() noexcept;

    WorldCounter(const WorldCounter&) = delete;
    WorldCounter& operator=(const WorldCounter&) = delete;
    WorldCounter(WorldCounter&&) = delete;
    WorldCounter& operator=(WorldCounter&&) = delete;

    [[nodiscard]] WorldId current() const noexcept;
    // 运行时解析环境变化后由调用处递增，返回新的世界计数。
    [[nodiscard]] WorldId advance();

private:
    WorldCounter() = default;

    WorldId current_ = 0;
};

} // baltam 命名空间
