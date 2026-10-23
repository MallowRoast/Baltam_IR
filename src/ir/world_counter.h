#pragma once

#include <cstdint>
#include <limits>
#include <stdexcept>

namespace baltam {

using WorldId = std::uint64_t;

// 全局世界计数；第一阶段由单线程调用。
class WorldCounter final {
public:
    [[nodiscard]] static WorldCounter& instance() noexcept {
        static WorldCounter counter;
        return counter;
    }

    WorldCounter(const WorldCounter&) = delete;
    WorldCounter& operator=(const WorldCounter&) = delete;
    WorldCounter(WorldCounter&&) = delete;
    WorldCounter& operator=(WorldCounter&&) = delete;

    [[nodiscard]] WorldId current() const noexcept { return current_; }
    // 运行时解析环境变化后由调用处递增，返回新的世界计数。
    [[nodiscard]] WorldId advance() {
        if (current_ == std::numeric_limits<WorldId>::max()) {
            throw std::overflow_error("世界计数已达到最大值");
        }
        return ++current_;
    }

private:
    WorldCounter() = default;

    WorldId current_ = 0;
};

} // baltam 命名空间
