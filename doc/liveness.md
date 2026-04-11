# Baltam_IR Liveness 说明

本文说明当前 `Liveness` analysis 的分析域、数据流方程和结果解释。

当前实现位于：

- [src/analysis/liveness.h](/home/zj/Desktop/Baltam_IR/src/analysis/liveness.h)
- [src/analysis/liveness.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/liveness.cpp)
- [src/analysis/name_analysis_utils.h](/home/zj/Desktop/Baltam_IR/src/analysis/name_analysis_utils.h)

它运行在已经通过 verifier 的 `NonSSA` CFG 上，分析对象是名字级 live set，而不是 SSA value 图。

## Liveness 的基础概念

当前 Liveness 讨论的是：

- “某个名字在某个程序点之后是否还会被使用”

因为当前 IR 还是 non-SSA：

- 同一个源码变量名可以多次被定义
- lowering 临时量也仍以名字文本传递

所以第一版 liveness 的分析域不是 SSA value，而是：

- `NamedValue.name`

### 1. `live_in[B]`

含义：

- 在进入基本块 `B` 之前，哪些名字必须已经有可用值

也就是说，如果一个名字在 `live_in[B]` 里，那么：

- 它要么会在 `B` 内先被使用后再定义
- 要么会在 `B` 的某个后继路径里继续被使用，且 `B` 自己没有先把它完全杀掉

### 2. `live_out[B]`

含义：

- 在离开基本块 `B` 之后，哪些名字仍然需要保留

也就是说，这些名字会被某个可达后继块继续需要。

### 3. upward-exposed uses

一个块里某个名字若出现“先使用、后定义”或者“只使用、不定义”，
那么这个名字就是该块的 upward-exposed use。

例如：

```text
body:
  y = x
```

这里：

- `x` 是 upward-exposed use
- `y` 是本块定义

因为：

- `x` 在本块里被读取之前，没有在本块中先定义

### 4. kill / def

若一个名字在当前块里被定义，那么它会把从后继传播回来的同名 live 信息“截断”掉。

例如：

```text
body:
  x = 1
  return x
```

这里：

- `x` 仍然会出现在 `live_in(body)` 吗，取决于定义前是否被使用
- 但从 `live_out(body)` 反推到 `live_in(body)` 时，`x` 会先被本块定义拦住

### 5. 当前分析域

第一版当前实现明确分析：

- 源码层变量名
- lowering 生成的临时量名

也就是：

- `NamedValue::UserVariable`
- `NamedValue::Temporary`

两者都统一按 `NamedValue.name` 文本处理。

## 它和 SSA 的关系

Liveness 在当前仓库里最直接的用途是：

- 为 `construct_untyped_ssa_module(...)` 提供 pruned phi 插入的过滤条件

也就是说：

- 某个名字即使从 dominance frontier 角度“可能需要 phi”
- 如果它在相应块并不 live-in
- 那这个 phi 往往就是无意义的

因此当前 Liveness 的职责是：

- 提供块级名字活跃信息

而不是：

- 直接做 phi 插入
- 直接生成 SSA 名字
- 直接做 DCE

## 当前 Liveness 的定位

当前 Liveness 是：

- 建立在 `CFGAnalysis` 之上的函数级 analysis

它的职责是：

- 为每个可达块计算 `live_in`
- 为每个可达块计算 `live_out`
- 统一给后续 SSA 构建和名字级优化提供块边界活跃信息

它不负责：

- 重建 CFG
- 处理不可达块的数据流语义
- 区分名字的动态类型
- 提供语句级或程序点级 liveness

也就是说，当前 Liveness 的角色是：

- “提供块级名字活跃信息”

而不是：

- “直接做 SSA 变换”

## 输入与前置条件

Liveness 的输入是：

- `Function`
- `CFGAnalysis::Result`

也就是说，它依赖：

- 结构合法的 CFG
- `postorder`
- `reverse_postorder`
- `is_reachable(...)`

这些都已经由：

- verifier
- CFGAnalysis

提供好了。

当前实现里，Liveness 通过：

- `analysis_manager.get<CFGAnalysis>(function)`

显式获取这个前置 analysis。

## 当前文件布局

当前实现位于：

- [src/analysis/liveness.h](/home/zj/Desktop/Baltam_IR/src/analysis/liveness.h)
- [src/analysis/liveness.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/liveness.cpp)

共用的名字提取辅助位于：

- [src/analysis/name_analysis_utils.h](/home/zj/Desktop/Baltam_IR/src/analysis/name_analysis_utils.h)

对应测试位于：

- [test/liveness_test.cpp](/home/zj/Desktop/Baltam_IR/test/liveness_test.cpp)

## 当前接口

当前接口如下：

```cpp
class Liveness {
public:
    struct Result {
        std::unordered_map<const BasicBlock*, std::vector<std::string>> live_in;
        std::unordered_map<const BasicBlock*, std::vector<std::string>> live_out;

        const std::vector<std::string>& live_in_of(const BasicBlock* block) const;
        const std::vector<std::string>& live_out_of(const BasicBlock* block) const;
    };

    Result run(Function& function, FunctionAnalysisManager& analysis_manager) const;
};
```

这里的设计原则是：

- 只保留块级 `live_in / live_out`
- 不提前混入语句级 in/out、phi 信息或 typed 信息
- 查询接口对空块和不可达块返回空列表
- 结果中的名字按字典序排序，保证行为稳定

## 每个成员的语义

### 1. `live_in`

类型：

- `std::unordered_map<const BasicBlock*, std::vector<std::string>>`

含义：

- 每个可达块入口处的名字活跃集合

约定：

- 所有可达块都会作为 key 出现
- 集合为空的块，其 value 是空向量
- 不可达块不会出现在 `live_in` 中

### 2. `live_out`

类型：

- `std::unordered_map<const BasicBlock*, std::vector<std::string>>`

含义：

- 每个可达块出口处的名字活跃集合

约定：

- 所有可达块都会作为 key 出现
- 集合为空的块，其 value 是空向量
- 不可达块不会出现在 `live_out` 中

### 3. `live_in_of(...)`

语义：

- 返回某个块的 `live_in`

约定：

- `nullptr` 输入返回空列表
- 不可达块返回空列表
- live 集合为空的块返回空列表

### 4. `live_out_of(...)`

语义：

- 返回某个块的 `live_out`

约定：

- `nullptr` 输入返回空列表
- 不可达块返回空列表
- live 集合为空的块返回空列表

## 当前分析规则

当前实现按 non-SSA 节点中的名字读写关系提取块级 `use / def`。

具体规则是：

### 会产生 definition 的节点

- `NumberNode::result()`
- `TextNode::result()`
- `AssignNode::dst()`
- `UnaryOpNode::result()`
- `BinOpNode::result()`
- `CallNode::outputs()`

### 会产生 use 的节点

- `AssignNode::src()`
- `UnaryOpNode::operand()`
- `BinOpNode::lhs()`
- `BinOpNode::rhs()`
- `CallNode::inputs()`
- `CondJumpNode::cond()`
- `ReturnNode::values()`

### 额外约定

- `CallNode` 为 `Indirect` 时，`callee()` 也会被当作名字 use
- `CallNode` 为 `Direct` 时，`callee()` 只是函数名文本，不计入名字 use
- `JumpNode` 不产生名字 use / def
- 函数签名里的 `inputs()` / `outputs()` 不是节点，不直接计入块内 def

## 当前算法

当前实现采用经典的块级 backward dataflow 迭代算法。

核心思路是：

- 先为每个块提取 upward-exposed uses 和 defs
- 再按 `postorder` 反复迭代 `live_in / live_out`
- 直到结果不再变化

### Step 1

先获取：

- `cfg = analysis_manager.get<CFGAnalysis>(function)`

### Step 2

对每个可达块扫描块内节点，计算：

- `upward_uses[B]`
- `defs[B]`

其中规则是：

- 先看到 use，且该名字尚未在当前块定义过，则加入 `upward_uses`
- 看到 def 后，把名字记入 `defs`

这一步会自然得到“块内先用后定义”的名字集合。

### Step 3

初始化所有可达块的：

- `live_in[B] = {}`
- `live_out[B] = {}`

### Step 4

按 `cfg.postorder` 迭代所有可达块，应用标准方程：

- `live_out[B] = union(live_in[S])`，其中 `S` 是 `B` 的可达后继
- `live_in[B] = upward_uses[B] union (live_out[B] - defs[B])`

### Step 5

如果某轮迭代里任一块的 `live_in` 或 `live_out` 发生变化，就继续下一轮。

直到整轮都不再变化，说明达到不动点。

### Step 6

把内部使用的无序集合：

- 转回 `std::vector<std::string>`
- 按字典序排序

这样做的好处是：

- 结果稳定
- 测试更容易写
- 调试输出更容易比较

## 为什么使用块级 fixpoint 迭代

对于当前仓库阶段，这个方案最合适，原因是：

- 当前还没有 SSA value 图
- 需要的只是块边界活跃信息
- 算法短、定义清楚、容易验证
- 已足够支撑当前 SSA 构建器

当前并不需要一开始就扩展成：

- 语句级 liveness
- 程序点级 live range
- typed value liveness

## 为什么按 `postorder` 迭代

因为当前是 backward dataflow：

- 某个块的结果依赖它的后继

而 `postorder` 恰好让：

- 后继块通常更早被访问

这样虽然本质上仍然是 fixpoint 迭代，但通常更容易快速收敛。

## 复杂度

若：

- 可达基本块数为 `B`
- 可达 CFG 边数为 `E`
- 所有可达块内出现的名字总数近似为 `N`

则第一版 Liveness 的成本可以近似理解为：

- 块内扫描成本：`O(N)`
- 每轮数据流传播成本：与 `B + E` 以及各块 live 集合大小相关

在当前实现采用无序集合的前提下，更合适的描述是：

- 时间复杂度：取决于迭代轮数和 live 集合总规模
- 空间复杂度：与所有块上保存的名字集合总规模成正比

对当前 non-SSA IR 阶段来说，这个成本是可接受的。

## 对不可达块的处理

当前实现和 CFGAnalysis / DominatorTree / DominanceFrontier 保持一致：

- 只在入口可达子图上建立 liveness 结果
- 不可达块不出现在 `live_in`
- 不可达块不出现在 `live_out`
- `live_in_of(unreachable)` 返回空列表
- `live_out_of(unreachable)` 返回空列表

## 与 pass manager 的失效关系

当前仓库已经有：

- [src/analysis/analysis_manager.h](/home/zj/Desktop/Baltam_IR/src/analysis/analysis_manager.h)
- [src/optimizer/pass_manager.h](/home/zj/Desktop/Baltam_IR/src/optimizer/pass_manager.h)

因此 Liveness 的缓存失效规则建议明确如下：

- 只要 pass 改了 CFG 形状，就不应保留 `Liveness`
- 只要 pass 改了任何节点上的名字 use / def 关系，也不应保留 `Liveness`

典型会导致失效的改动包括：

- 增加/删除 `BasicBlock`
- 修改 `predecessors()/successors()`
- 替换 terminator 并改变 CFG 边
- 修改 `AssignNode` / `BinOpNode` / `CallNode` 等节点的输入输出名字
- 增删块内普通指令

相反，只有在 pass 完全不改：

- CFG 形状
- 块内名字读写关系

时，`Liveness` 才可以安全复用。

## 当前测试覆盖

当前测试位于：

- [test/liveness_test.cpp](/home/zj/Desktop/Baltam_IR/test/liveness_test.cpp)

覆盖的 CFG 形状包括：

### 1. 线性 CFG

验证：

- 参数名字会沿直线传播到后续块
- 定义后的名字会进入后继块的 `live_in`

### 2. 分支 / 合流 CFG

验证：

- 某个在两条分支里都被使用的临时量，会出现在分支入口的 `live_in`
- 合流块返回的名字，会反向传播到各个前驱的 `live_out`

### 3. 循环 CFG

验证：

- 回边会把名字活跃信息传回 loop header
- fixpoint 迭代能正确收敛

### 4. 含不可达块的 CFG

验证：

- 不可达块不出现在结果映射中
- 对不可达块的查询返回空列表

## 和后续 analysis / SSA 的关系

当前 Liveness 是后续这些步骤的直接前置之一：

- `construct_untyped_ssa_module(...)`
- pruned phi 过滤
- 名字级死代码消除

其中最直接的用途就是：

- 把 `DominanceFrontier` 提供的“结构上可能需要 phi 的块”
- 再经过 liveness 过滤成“语义上真的需要 phi 的块”

## 当前结论

当前仓库里的 Liveness 可以概括为：

- 输入：
  - `Function`
  - `CFGAnalysis::Result`
- 输出：
  - `block -> live_in names`
  - `block -> live_out names`
- 范围：
  - 只覆盖入口可达子图
- 分析域：
  - `NamedValue.name`
- 作用：
  - 为当前 SSA 构建器和后续名字级优化提供块级活跃信息

它是从 `DominanceFrontier` 继续走向当前 SSA 构建器时不可缺的一块名字级 analysis。
