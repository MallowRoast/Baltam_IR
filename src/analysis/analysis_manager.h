#ifndef BALTAM_IR_ANALYSIS_ANALYSIS_MANAGER_H
#define BALTAM_IR_ANALYSIS_ANALYSIS_MANAGER_H

#include <memory>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "ir/ir.h"

namespace baltam {
namespace analysis {

/**
 * @brief 描述一个 pass 执行后仍然保持有效的 analysis 集合。
 *
 * 该对象是 pass 和 `FunctionAnalysisManager` 之间的失效协议：
 *
 * - pass 通过 `PreservedAnalyses` 声明“哪些 analysis 结果仍可复用”
 * - analysis manager 根据这份声明决定删掉哪些缓存
 *
 * 对外接口保持模板形式，调用方直接按 analysis 类型声明保留关系；
 * 底层 `std::type_index` 细节只对本类和 `FunctionAnalysisManager` 可见。
 */
class PreservedAnalyses {
public:
    /**
     * @brief 构造“全部 analysis 都被保留”的结果。
     */
    static PreservedAnalyses all();

    /**
     * @brief 构造“没有任何 analysis 被保留”的结果。
     */
    static PreservedAnalyses none();

    /**
     * @brief 判断当前对象是否声明“保留所有 analysis”。
     */
    bool preserves_all() const;

    /**
     * @brief 按 analysis 类型声明该结果在 pass 之后仍然有效。
     */
    template <typename AnalysisT>
    void preserve() {
        preserve_impl(typeid(AnalysisT));
    }

    /**
     * @brief 判断某个 analysis 类型是否被标记为仍然有效。
     */
    template <typename AnalysisT>
    bool is_preserved() const {
        return is_preserved_impl(typeid(AnalysisT));
    }

private:
    friend class FunctionAnalysisManager;

    /**
     * @brief 按运行时类型编号记录一项 analysis 被保留。
     *
     * 这是模板接口背后的底层实现，只暴露给本类内部和
     * `FunctionAnalysisManager` 使用。
     */
    void preserve_impl(std::type_index analysis_id);

    /**
     * @brief 按运行时类型编号查询某项 analysis 是否被保留。
     *
     * 这是模板接口背后的底层实现，只暴露给本类内部和
     * `FunctionAnalysisManager` 使用。
     */
    bool is_preserved_impl(std::type_index analysis_id) const;

    /**
     * @brief 是否保留全部 analysis。
     */
    bool preserve_all_ = false;

    /**
     * @brief 当 `preserve_all_` 为 false 时，记录被显式保留的 analysis 集合。
     */
    std::unordered_set<std::type_index> preserved_;
};

/**
 * @brief 按函数缓存 analysis 结果并负责统一失效的管理器。
 *
 * 该类参考 LLVM 新 PassManager 的设计思路：
 *
 * - analysis 结果按 `Function*` 维度缓存
 * - 每种 analysis 由其自身类型唯一标识
 * - pass 结束后通过 `PreservedAnalyses` 决定哪些缓存需要失效
 */
class FunctionAnalysisManager {
public:
    /**
     * @brief 创建一个空的函数级 analysis 管理器。
     */
    FunctionAnalysisManager() = default;

    /**
     * @brief 禁止拷贝，避免无意复制内部缓存。
     */
    FunctionAnalysisManager(const FunctionAnalysisManager&) = delete;

    /**
     * @brief 禁止赋值，避免无意复制内部缓存。
     */
    FunctionAnalysisManager& operator=(const FunctionAnalysisManager&) = delete;

    /**
     * @brief 获取某个函数上的 analysis 结果，必要时自动计算并缓存。
     *
     * 若缓存中已有对应结果则直接返回；否则构造 `AnalysisT` 并调用
     * `AnalysisT::run(Function&, FunctionAnalysisManager&)` 生成结果。
     */
    template <typename AnalysisT>
    const typename AnalysisT::Result& get(Function& function) {
        FunctionCache& cache = cache_[&function];
        const std::type_index id = typeid(AnalysisT);

        auto it = cache.find(id);
        if (it != cache.end()) {
            return static_cast<AnalysisResultModel<AnalysisT>*>(it->second.get())->result;
        }

        AnalysisT analysis;
        auto model = std::make_unique<AnalysisResultModel<AnalysisT>>(
            analysis.run(function, *this));
        AnalysisResultModel<AnalysisT>* raw_model = model.get();
        cache.emplace(id, std::move(model));
        return raw_model->result;
    }

    /**
     * @brief 让某个函数上指定 analysis 类型的缓存失效。
     */
    template <typename AnalysisT>
    void invalidate(Function& function) {
        auto function_it = cache_.find(&function);
        if (function_it == cache_.end()) {
            return;
        }

        function_it->second.erase(typeid(AnalysisT));
        if (function_it->second.empty()) {
            cache_.erase(function_it);
        }
    }

    /**
     * @brief 根据 `PreservedAnalyses` 让某个函数上的缓存按需失效。
     */
    void invalidate(Function& function, const PreservedAnalyses& preserved);

    /**
     * @brief 清除某个函数上的全部 analysis 缓存。
     */
    void invalidate_all(Function& function);

    /**
     * @brief 清除当前管理器持有的全部函数缓存。
     */
    void clear();

private:
    /**
     * @brief 所有分析结果缓存对象的非模板基类。
     */
    struct AnalysisResultConcept {
        virtual ~AnalysisResultConcept() = default;
    };

    /**
     * @brief 某个具体 analysis 结果的类型擦除包装。
     */
    template <typename AnalysisT>
    struct AnalysisResultModel final : AnalysisResultConcept {
        /**
         * @brief 用一份 analysis 结果构造缓存包装对象。
         */
        explicit AnalysisResultModel(typename AnalysisT::Result result_in)
            : result(std::move(result_in)) {}

        /**
         * @brief 被缓存的具体 analysis 结果。
         */
        typename AnalysisT::Result result;
    };

    /**
     * @brief 单个函数对应的“analysis 类型 -> analysis 结果”缓存表。
     */
    using FunctionCache =
        std::unordered_map<std::type_index, std::unique_ptr<AnalysisResultConcept>>;

    /**
     * @brief 全局函数缓存表，按 `Function*` 组织各自的 analysis 结果。
     */
    std::unordered_map<Function*, FunctionCache> cache_;
};

}  // namespace analysis
}  // namespace baltam

#endif
