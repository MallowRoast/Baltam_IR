#include "ir/world_counter.h"

#include <limits>
#include <stdexcept>

namespace baltam {

WorldCounter& WorldCounter::instance() noexcept {
    static WorldCounter counter;
    return counter;
}

WorldId WorldCounter::current() const noexcept {
    return current_;
}

WorldId WorldCounter::advance() {
    if (current_ == std::numeric_limits<WorldId>::max()) {
        throw std::overflow_error("世界计数已达到最大值");
    }
    return ++current_;
}

} // baltam 命名空间
