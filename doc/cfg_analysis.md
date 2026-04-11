# Baltam_IR CFGAnalysis 说明

本文只说明当前仓库里 `CFGAnalysis` 的职责、输入前提和结果结构。

当前实现位于：

- [src/analysis/cfg_analysis.h](/home/zj/Desktop/Baltam_IR/src/analysis/cfg_analysis.h)
- [src/analysis/cfg_analysis.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/cfg_analysis.cpp)

它运行在已经通过 verifier 的 `NonSSA` 函数上，直接读取显式 CFG，不负责重新推导或修复控制流边。

## 目标与定位

CFGAnalysis 应该是当前 non-SSA 阶段第一个正式的函数级 analysis。

它的职责是：

- 从 `Function` 的显式 CFG 中提取可复用的遍历结果
- 为后续前向/反向数据流分析提供统一遍历顺序
- 为 `DominatorTree`、`DominanceFrontier`、`Liveness`、`DefUse` 提供公共底座

它不负责：

- 修复 CFG
- 删除不可达块
- 从 terminator 重新构建边
- 做名字级数据流分析

也就是说，CFGAnalysis 是“读当前 CFG 并整理结构信息”的 analysis，而不是 CFG transform pass。

## 输入与前置条件

CFGAnalysis 的输入是单个 `Function`。

当前实现依赖的最小前置条件为：

- `function.entry_block()` 非空
- `entry_block()` 属于 `function.blocks()`
- `predecessors()` / `successors()` 基本双向一致
- 所有边目标都属于当前函数

这些结构合法性前提本质上已经由 verifier 负责：

- [src/analysis/verifier.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/verifier.cpp)

因此 CFGAnalysis 自己不应重复整套 verifier，只需要做极少量防御性检查：

- 没有入口块时直接抛错
- 入口块不属于当前函数时直接抛错

当前实现直接假定：

`CFGAnalysis::run(...)` 的输入函数已经通过 verifier。

## 当前文件布局

当前实现位于：

- [src/analysis/cfg_analysis.h](/home/zj/Desktop/Baltam_IR/src/analysis/cfg_analysis.h)
- [src/analysis/cfg_analysis.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/cfg_analysis.cpp)

并保持和当前 `FunctionAnalysisManager` 的风格一致：

- analysis 自身定义 `Result`
- 由 `FunctionAnalysisManager::get<CFGAnalysis>(function)` 统一缓存和复用

## 当前接口

当前接口如下：

```cpp
class CFGAnalysis {
public:
    struct Result {
        std::vector<const BasicBlock*> postorder;
        std::vector<const BasicBlock*> reverse_postorder;
        std::unordered_map<const BasicBlock*, std::size_t> rpo_index;

        bool is_reachable(const BasicBlock* block) const;
    };

    Result run(Function& function, FunctionAnalysisManager&) const;
};
```

这里保持几个原则：

- `Result` 只保存 CFG 结构信息，不保存名字级分析结果
- 使用 `const BasicBlock*` 表达“analysis 只读视图”
- `run(...)` 接受 `FunctionAnalysisManager&`，与后续其他 analysis 接口统一
- 输出接口采用“平衡版”：
  - 保留 `postorder`
  - 保留 `reverse_postorder`
  - 保留 `rpo_index`
  - 删除其余可间接推导成员

## 输出语义

### 1. `postorder`

含义：

- 从入口沿 `successors()` 做 DFS 时，按“退出节点时压栈”得到的后序遍历结果

作用：

- 供反向数据流分析使用
- 供某些 CFG 结构算法直接使用

### 2. `reverse_postorder`

含义：

- `postorder` 反转后的结果

作用：

- 供前向分析使用
- 供 dominator 类 analysis 使用

### 3. `rpo_index`

含义：

- `reverse_postorder[i]` 在 RPO 中的索引映射

作用：

- 快速比较块在 RPO 中的相对位置
- 避免后续 dominator / dataflow 每次线性查找

### 4. `is_reachable(...)`

语义：

- 输入块是否出现在 RPO 结果中

当前行为：

- `nullptr` 返回 `false`
- 通过 `rpo_index.contains(...)` 或等价逻辑判断

## 为什么只保留这三个成员

当前采用“平衡版”而不是“最小版”或“全量缓存版”，原因是：

- `postorder` 是反向分析的直接输入
- `reverse_postorder` 是前向分析和 dominator 的直接输入
- `rpo_index` 可以避免后续 analysis 反复扫描 RPO 向量

而以下信息都可以被间接推导，因此不再单独保存：

- 入口块：
  - 可直接从 `function.entry_block()` 读取
- 可达块集合：
  - 可由 `rpo_index` 的 key 集合表达
- 可达块列表：
  - 可直接用 `reverse_postorder`
- 显式 `reachable_set`：
  - `is_reachable(...)` 可以直接查询 `rpo_index`

## 核心算法

### Step 1

取出入口块和函数块列表：

- `entry = function.entry_block()`
- `blocks = function.blocks()`

如果入口为空，直接抛错。

### Step 2

构造一张 `known_blocks` 集合：

- 从 `function.blocks()` 收集全部块指针

作用：

- 防御性确认入口块是否属于当前函数
- 未来如需做轻量断言，也能快速判断 CFG 边目标是否合法

### Step 3

从入口块沿 `successors()` 做 DFS：

- 遍历其后继
- DFS 退出该块时，把它压入 `postorder`

建议遍历顺序：

- 直接沿当前 `successors()` 向量顺序遍历

这样可以保证：

- 结果稳定
- 与 lowering 当前生成 CFG 的顺序一致

### Step 4

构造：

- `reverse_postorder = reverse(postorder)`

### Step 5

遍历 `reverse_postorder`，建立：

- `rpo_index[block] = index`

## 伪代码

```cpp
CFGAnalysis::Result CFGAnalysis::run(Function& function, FunctionAnalysisManager&) const {
    Result result;
    const BasicBlock* entry = function.entry_block();

    if (entry == nullptr) {
        throw std::runtime_error(...);
    }

    std::unordered_set<const BasicBlock*> known_blocks;
    for (const auto& block : function.blocks()) {
        if (block != nullptr) {
            known_blocks.insert(block.get());
        }
    }

    if (known_blocks.find(entry) == known_blocks.end()) {
        throw std::runtime_error(...);
    }

    std::unordered_set<const BasicBlock*> visited;
    build_postorder(entry, known_blocks, visited, result.postorder);

    result.reverse_postorder.assign(result.postorder.rbegin(), result.postorder.rend());

    for (std::size_t i = 0; i < result.reverse_postorder.size(); ++i) {
        result.rpo_index[result.reverse_postorder[i]] = i;
    }

    return result;
}
```

## 复杂度

若：

- 基本块数为 `B`
- CFG 边数为 `E`

则 CFGAnalysis 的主成本为：

- 时间复杂度：`O(B + E)`
- 空间复杂度：`O(B)`

这已经足够作为所有后续 analysis 的公共底座。

## 为什么不直接重建 CFG

当前 IR 已经显式保存：

- `BasicBlock::successors()`
- `BasicBlock::predecessors()`

并且 verifier 已检查：

- CFG 边双向一致
- terminator 目标属于当前函数
- terminator 目标在 successor 列表中

因此 CFGAnalysis 最合理的职责边界是：

- 信任显式 CFG
- 使用显式 CFG
- 输出遍历与索引结果

而不是：

- 每次分析时重新从 terminator 推断 CFG

后者会带来两个问题：

- 重复 verifier 已经做过的结构判断
- analysis 结果可能和容器内显式 CFG 脱节

## 与后续 analysis 的关系

CFGAnalysis 预计会成为下列 analysis 的共同输入：

- `DominatorTree`
- `DominanceFrontier`
- `Liveness`
- `DefUse`

其中最直接的关系是：

### 对 `DominatorTree`

- 使用 `reverse_postorder`
- 使用 `rpo_index`
- 只在可达块上构造支配关系

### 对 `Liveness`

- 使用 `postorder` 做反向迭代
- 使用 `is_reachable(...)` 跳过不可达块

### 对 `DefUse`

- 可基于 `reverse_postorder` 或 `is_reachable(...)` 限定第一版 analysis 范围

## 与 pass manager 的失效关系

当前仓库已经有：

- [src/analysis/analysis_manager.h](/home/zj/Desktop/Baltam_IR/src/analysis/analysis_manager.h)
- [src/optimizer/pass_manager.h](/home/zj/Desktop/Baltam_IR/src/optimizer/pass_manager.h)

因此 CFGAnalysis 的缓存失效规则建议明确如下：

- 只要 pass 改了 `entry_block`
- 或增加/删除 `BasicBlock`
- 或修改 `predecessors()/successors()`
- 或替换 terminator 并导致 CFG 边变化

就不应保留 `CFGAnalysis`。

相反，如果 pass 只改 block 内普通指令，而不改 CFG 形状，则可以保留 `CFGAnalysis`。

## 建议测试范围

第一版建议至少覆盖以下 4 类函数 CFG：

### 1. 线性 CFG

例如：

- `entry -> b1 -> b2 -> ret`

预期：

- 所有块都可达
- `postorder` 与 `reverse_postorder` 结果稳定

### 2. 菱形 CFG

例如：

- `entry -> then`
- `entry -> else`
- `then -> merge`
- `else -> merge`

预期：

- `merge` 在可达集合中
- RPO 顺序稳定

### 3. 带回边的循环 CFG

例如：

- `entry -> header -> body -> header`
- `header -> exit`

预期：

- DFS 不死循环
- 回边不影响正确生成 `postorder`

### 4. 含不可达块的 CFG

例如：

- `Function::blocks()` 中存在一个与入口无关的孤立块

预期：

- 该块不出现在 `rpo_index`
- 不出现在 `postorder / reverse_postorder / rpo_index`

## 建议实施顺序

### Step 1

增加：

- `src/analysis/cfg_analysis.h`
- `src/analysis/cfg_analysis.cpp`

先把 `Result` 和 `run(...)` 接口定住。

### Step 2

实现基于 `successors()` 的 DFS 版本：

- `reachable_set`
- `postorder`
- `reverse_postorder`
- `rpo_index`
- `reachable_blocks`

### Step 3

补最小测试：

- 线性
- 菱形
- 循环
- 不可达块

### Step 4

再让 `DominatorTree` 以 `CFGAnalysis` 为前置 analysis 接入。

## 当前结论

对当前仓库来说，CFGAnalysis 最合理的方案是：

- 输入一个已经通过 verifier 的 `Function`
- 直接使用显式 `predecessors()/successors()` CFG
- 输出 `postorder / reverse_postorder / rpo_index / is_reachable`
- 作为后续 dominator、liveness、SSA 构建的公共底座

它应当是当前 non-SSA analysis 基建里的第一块砖，而不是临时散落在各个 pass 里的辅助逻辑。
