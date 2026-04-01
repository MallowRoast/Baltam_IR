# Baltam_IR SSA 方案整理

## 目标

本文档整理 Baltam_IR 后续引入 SSA 的建议方案，重点回答下面几个问题：

- 当前 IR 是否值得 SSA 化
- SSA 应该在什么阶段引入
- SSA 化应以什么形式落到现有代码结构中
- 在 SSA 之前和之后，哪些优化 pass 最适合实现
- 如何控制改造风险，避免一次性重写解释器和 lowering

本文档面向当前仓库已有实现：

- `AST -> IR lowering`
- IR 文本打印
- IR 解释执行
- 显式 `BasicBlock + Jump/CondJump/Return`

并将 SSA 视为“优化 IR 的演进方向”，而不是要求立刻把整个系统一次性改写成 SSA。

## 当前判断

### 1. 当前 IR 暂时还能继续工作，但已经开始接近 SSA 改造点

当前 IR 已经具备几个很重要的前提：

- 显式 CFG
- 显式基本块
- 显式终结指令
- 指令级源码位置信息

这些基础设施已经足够支撑后续做 dominator、phi 插入、rename 和 def-use 分析。

但当前 IR 的数据流模型仍然是“值 SSA 与名字环境并存”的混合形式，典型特征包括：

- 大多数计算节点已经通过 `ValueRef` 显式传递依赖
- `PhiInstruction` 已经进入 IR，并在结构化 lowering 中开始使用
- `AssignInstruction` / `BindingInstruction` 仍然保留名字环境语义
- 解释器执行时同时维护 frame 中的名字表和值槽表

这意味着当前 IR 仍然是“变量可多次赋值”的非 SSA 形式。

### 2. 如果只做 very early passes，可以先不强制 SSA

以下优化即使在非 SSA 形式下也可以先做：

- 局部常量折叠
- 单基本块内常量传播
- 可达块清理
- 很保守的死代码消除

所以从“短期交付最小可用优化器”的角度看，不必为了这些 pass 立刻重写 IR。

### 3. 如果准备继续做全局优化，SSA 很快会变得值得

一旦后续目标包括：

- 跨基本块常量传播
- 分支合流后的数据流分析
- 更可靠的死代码消除
- 稀疏条件常量传播
- 循环相关优化
- 为未来 JIT 或专门化 lowering 准备更干净的优化输入

那么 SSA 会明显降低实现复杂度。

结论可以概括为：

- 现在不必把“执行 IR”立刻全部 SSA 化
- 但应尽快规划“优化 IR”向 SSA 演进
- 不建议等到引入 JIT 之后再开始 SSA 化

### 4. 当前仓库已经进入 value-based 迁移阶段

和最初只有“名字环境 IR”的状态相比，当前仓库里已经有一批 SSA 前置基础设施落地：

- `Instruction` 已经支持 `ValueId` / `ValueRef` / `InstValue`
- 解释器的 `Frame` 同时维护名字表和 value 槽表
- 大多数产值指令在 lowering 阶段就会拿到稳定的 `ValueId`
- `UnaryOp`、`BinOp`、`Assign`、`Call`、`CondJump`、`Return` 都已经能显式携带 `ValueRef`
- `CallInstruction`、`ReturnInstruction` 以及若干核心表达式节点已经支持 ref-first 构造
- 解释器和打印器已经优先通过 ref-first 访问 API 读取调用输入、返回值、条件和表达式依赖
- `UnaryOp`、`BinOp`、`Assign`、`CondJump`、`Return` 这些核心节点已经完全转到 `ValueRef` 主路径
- `CallInstruction` 的输入已经收敛成纯 `ValueRef` 列表；调用输出绑定也已经从调用节点内部迁移为 lowering 显式生成的 `AssignInstruction`
- 新 lower 出来的 `UnaryOp`、`BinOp`、`Assign`、`CondJump` 都不再保存兼容操作数指针
- 解释器在 value 槽还没写入时，已经可以通过 `ValueId -> 定义指令` 回溯物化对应值
- `ReturnInstruction` 已经收敛成纯 `ValueRef` 返回列表；lowering、解释器和打印器都不再依赖显式返回值指针列表

这意味着本文档里早期关于 value-based 迁移的很多设计目标已经大部分完成，后续重点应转向分析层、verifier 和独立的 SSA pass。

## 为什么 SSA 值得做

## 数据流更显式

在 SSA 中，每个定义只写一次，合流点通过 `phi` 显式表达。这样可以避免很多“这个名字当前到底对应哪次赋值”的隐式推理。

对当前项目最直接的好处是：

- 常量传播更容易做成全局分析
- DCE 可以基于 def-use 链而不是变量名回溯
- 冗余赋值和未使用中间值更容易识别
- 分支与循环的合流行为更容易建模

## 更适合作为优化 IR

SSA 的价值主要体现在“分析与优化”，不在于解释器是否必须直接执行 SSA。

因此，对 Baltam_IR 更合理的定位是：

- 非 SSA IR：更贴近当前 lowering 和解释执行
- SSA IR：优化用 IR，服务于全局数据流分析和后续专门化

这也意味着当前解释器不一定要第一时间重写成“直接执行 SSA IR”。

## 更适合后续 JIT

SSA 不是 JIT 专属技术，但 JIT 往往更喜欢输入是已整理过的数据流 IR。提前把优化层 SSA 化，有几个长期好处：

- 热点分析后更容易做 specialized lowering
- 更容易建立值版本、guard 和去装箱路径
- 后续降到更低层 IR 或机器码时边界更清晰

## 当前阶段的主要约束

## 1. 局部值路径和名字环境还没有完全分层

当前仓库已经把大量计算语义迁移到 `ValueId` / `ValueRef` 上，但纯局部值路径和名字环境语义还没有完全拆开。

目前更准确的状态是：

- 表达式和调用结果已经可以稳定定义值
- `AssignInstruction` 仍然承担名字绑定语义
- `BindingInstruction` 仍然承担运行时名字读取语义

因此，这一阶段更准确地说是“near-SSA / value-based 迁移中”，而不是纯 value IR。

## 2. 多返回值调用已经 value-based，但后续绑定仍可能回到名字层

MATLAB-like 语言的多返回值调用仍然是 SSA 设计中的关键点。

当前 `CallInstruction` 已经具备：

- 多结果值定义
- `ValueRef` 输入参数
- ref-first 构造路径

但在当前 lowering 中，调用结果在需要时仍会继续绑定回源码名字。因此真正要进一步推进 SSA，重点已经不是重新设计调用节点本身，而是把“调用结果值”和“后续名字绑定”分层。

## 3. 解释器仍然是“名字环境 + value 槽位”双轨执行

当前解释器已经能按 `ValueRef` 物化和读取值，但仍然保留可观察的名字环境语义。

因此改造时需要继续坚持一个原则：

- 不要把“SSA 化”与“解释器重写”绑成同一次大改

更稳妥的做法仍然是先让 SSA 成为优化层，再逐步决定优化结果如何接回执行链。

## 推荐总体架构

推荐把 IR 分成两层，而不是只保留一层：

### 1. 执行 IR

职责：

- 承接现有 lowering
- 保持较强源码可读性
- 继续服务于解释器执行
- 作为 fallback IR 保留动态语义

特点：

- 可非 SSA
- 允许按变量名建模
- 更贴近源码工作区语义

### 2. 优化 IR

职责：

- 进行常量折叠
- 进行常量传播
- 进行 DCE
- 做 CFG 简化
- 为未来热点专门化/JIT 提供更规整输入

特点：

- 建议做 SSA
- 更偏 value-based IR
- 显式 `phi`
- 显式 def-use

推荐主线：

`AST -> 执行 IR -> SSA 优化 IR -> 优化 passes -> 回写/降级到执行 IR 或继续 lower 到后端`

在项目当前阶段，也可以先只做到：

`AST -> 执行 IR -> SSA 优化 IR -> 打印/验证`

先把基础设施搭起来，再决定是否把优化结果重新喂给解释器。

## SSA 方案设计

## 1. SSA 的适用范围

第一阶段建议只覆盖：

- 函数内局部变量
- 形参
- 返回值名
- lowering 过程中生成的隐藏临时变量

第一阶段不建议急着 SSA 化下面这些潜在更动态的实体：

- 全局变量语义
- `eval` 一类动态名字注入
- `assignin` / `evalin` 风格行为
- 复杂对象属性写入
- 动态索引写回的别名问题

也就是说，第一阶段目标应是：

- 在“当前已能稳定 lower 的局部函数/脚本子集”上建立 SSA

## 2. SSA 的基本对象

建议引入下面几个概念。

### ValueId

每个 SSA 值一个 `ValueId`，代表一条定义产生的结果。

典型来源：

- 常量
- 二元运算
- 一元运算
- 调用结果
- `phi`
- 参数

### LocalName

保留源码层名字，主要用于：

- debug 打印
- 源码映射
- rename 时追踪“这个 SSA 版本原来属于哪个源码名字”

### PhiInstruction

在控制流合流块显式引入：

- `%x3 = phi [bb1: %x1], [bb2: %x2]`

phi 不表示运行时真正执行的普通算术，而是控制流相关的值选择。

当前仓库已经落了一版可执行的 `PhiInstruction`：

- 解释器会在 block 入口按前驱块先解析 phi
- lowering 已经开始在 `if` / `switch` 的 merge block 生成 phi
- 当前生成范围是“各分支都赋值的名字”，先避免未定义名字快照带来的语义回归

### BlockArgument 备选

如果后续更想走现代 IR 风格，也可以把 `phi` 进一步抽象成 block arguments。

但对当前代码库来说，第一阶段直接引入 `PhiInstruction` 更容易落地，也更容易和现有 `BasicBlock` 结构兼容。

## 3. 从当前 near-SSA 状态走向完整 SSA

当前仓库已经完成了 value-based 的第一轮迁移，所以下一步不再是“重新把 IR 做成结果值导向”，而是把现有结果值模型接到通用 SSA 基建上。

后续重点应放在：

- dominator tree
- dominance frontier
- liveness
- phi 插入
- rename pass

完成之后，局部变量引用才能系统性地从“按当前路径名字查值”转成“直接引用具体 SSA 定义”。

## 4. 参数、返回值与调用的处理

### 参数

函数参数天然可视为 SSA 定义。

建议：

- 每个输入参数在入口块都有一个初始 SSA 值
- 打印时保留源码参数名作为注释或符号名

### 返回值

MATLAB-like 语言经常通过“给输出变量名赋值”来形成返回值。

第一阶段建议不要急着改变源码语义，而是：

- 返回值名在 IR 中先视为普通局部名字
- 函数结束时，根据当前版本读取这些名字对应的 SSA 值
- `ReturnInstruction` 显式携带返回 SSA 值列表

这样更利于优化分析，也更接近后续后端接口。

### 多返回值调用

当前 `CallInstruction` 已经能定义多个结果值，因此这里的重点不再是重做调用表示，而是：

- 把调用结果稳定视为普通 SSA 值定义
- 让后续优化尽量直接消费这些结果值
- 只在确实需要保留源码可观察语义时，再把结果绑定回名字层

## 5. 名字与调试信息

SSA 化之后，源码名字不能丢。

建议每个 SSA 定义至少保留：

- 原始名字，如 `x`
- SSA 版本号，如 `x.3`
- 源码位置
- 可选的类型/profile 注释

文本 IR 打印时建议显示：

- `%x.1 = const 1`
- `%x.2 = add %x.1, %y.0`
- `%x.3 = phi [then: %x.1], [else: %x.2]`

这样既保留调试可读性，也不会失去源码映射。

## 需要新增的基础设施

## 1. CFG 分析基础设施

SSA 化前应补齐：

- 反向后继/前驱遍历接口
- 可达块分析
- dominator 计算
- dominance frontier 计算

虽然当前 `BasicBlock` 已经有前驱和后继，但还缺“分析结果对象”这一层。

建议新增 `analysis/` 或 `optimizer/analysis/` 目录，集中放：

- `cfg_analysis`
- `dom_tree`
- `dom_frontier`
- `use_def_analysis`

### 1.1 这 5 个基础 analysis 的输入与输出

下面这 5 个 analysis 可以看成后续 `BuildPrunedSSA` 和第一批中端优化的最小分析层。

设计时建议统一遵守一个原则：

- 输入尽量直接使用当前 `Function / BasicBlock / Instruction / ValueRef`
- 输出尽量做成稳定的 `Result` 对象
- `Result` 既要能被 pass 直接查询，也要能被 verifier / 调试打印复用

#### `CFGAnalysis`

基本作用：

- 把函数的控制流骨架整理成可复用的分析结果
- 为后续 dominator、liveness、可达块清理等 pass 提供统一入口

建议输入：

- 一个已经 basic well-formed 的 `Function`
- 依赖当前 IR 上已有的：
  - `entry_block()`
  - `blocks()`
  - `predecessors()`
  - `successors()`

建议输出：

- `reachable_blocks`
- `reverse_postorder`
- `postorder`
- `block_to_rpo_index`
- 一个统一的 `is_reachable(BasicBlock*)` 查询接口

第一版 `Result` 可以长成：

```cpp
struct CFGAnalysis {
    struct Result {
        std::vector<BasicBlock*> reachable_blocks;
        std::vector<BasicBlock*> reverse_postorder;
        std::vector<BasicBlock*> postorder;
        std::unordered_map<BasicBlock*, std::size_t> rpo_index;

        bool is_reachable(BasicBlock* block) const;
    };
};
```

这里的重点是：

- `reachable_blocks` 给 dead block 清理和 verifier 用
- `reverse_postorder` 给大多数前向数据流分析用
- `postorder` 给某些反向分析或构建 dominator 辅助用

#### `DominatorTree`

基本作用：

- 判断某个块是否支配另一个块
- 为 SSA rename、dominance frontier、代码移动等分析提供基础

建议输入：

- 一个 `Function`
- `CFGAnalysis::Result`

建议输出：

- 每个块的 `immediate dominator`
- dominator tree 的 children
- `dominates(A, B)` 查询
- 方便支配树遍历的顺序结果

第一版 `Result` 可以长成：

```cpp
struct DominatorTreeAnalysis {
    struct Result {
        std::unordered_map<BasicBlock*, BasicBlock*> idom;
        std::unordered_map<BasicBlock*, std::vector<BasicBlock*>> children;
        std::vector<BasicBlock*> preorder;
        std::vector<BasicBlock*> postorder;

        BasicBlock* immediate_dominator(BasicBlock* block) const;
        bool dominates(BasicBlock* lhs, BasicBlock* rhs) const;
    };
};
```

这里的重点是：

- `idom` 是所有后续 dominator 相关 analysis 的核心主结果
- `children` 让 SSA rename 可以直接沿支配树递归
- `dominates()` 要做成公共查询接口，而不是让每个 pass 自己回溯 `idom`

#### `DominanceFrontier`

基本作用：

- 找出“定义开始不再严格支配下去”的控制流合流边界
- 这是 `phi` 插入位置分析的直接输入

建议输入：

- 一个 `Function`
- `CFGAnalysis::Result`
- `DominatorTreeAnalysis::Result`

建议输出：

- `BasicBlock* -> frontier blocks`
- 一个统一的 `frontier_of(block)` 查询接口

第一版 `Result` 可以长成：

```cpp
struct DominanceFrontierAnalysis {
    struct Result {
        std::unordered_map<BasicBlock*, std::vector<BasicBlock*>> frontier;

        const std::vector<BasicBlock*>& frontier_of(BasicBlock* block) const;
    };
};
```

这里的重点是：

- `BuildPrunedSSA` 会用“定义块集合 + dominance frontier”决定 `phi` 插入块
- 第一版不需要做过度复杂的增量维护，只要结果正确即可

#### `Liveness`

基本作用：

- 判断某个名字/值在块入口或块出口是否仍然“活着”
- 为 `Pruned SSA` 避免插入无意义 `phi`
- 也为后续 DCE、局部清理和更细粒度优化打基础

建议输入：

- 一个 `Function`
- `CFGAnalysis::Result`
- 一个明确的“分析对象域”

这里要特别注意：第一版 `Liveness` 不一定要直接对所有 `ValueId` 做。

对当前项目，更合理的第一版是：

- 对“可提升为 SSA 的局部名字集合”做 liveness
- 也就是：
  - 形参名
  - 局部变量名
  - 返回值名
  - lowering 生成的隐藏临时名

建议输出：

- 每个块的 `live_in`
- 每个块的 `live_out`
- 可选地保留 `block_use` / `block_def`

第一版 `Result` 可以长成：

```cpp
struct LivenessAnalysis {
    using NameSet = std::unordered_set<std::string>;

    struct Result {
        std::unordered_map<BasicBlock*, NameSet> block_use;
        std::unordered_map<BasicBlock*, NameSet> block_def;
        std::unordered_map<BasicBlock*, NameSet> live_in;
        std::unordered_map<BasicBlock*, NameSet> live_out;

        bool is_live_in(BasicBlock* block, const std::string& name) const;
        bool is_live_out(BasicBlock* block, const std::string& name) const;
    };
};
```

这里的重点是：

- 在真正 rename 成 SSA 版本之前，`liveness` 更自然地以“源码层局部名字”为域
- `BuildPrunedSSA` 需要的是“这个名字在该合流块是否 live-in”，而不是先有 SSA 再做 liveness

#### `DefUse`

基本作用：

- 把值定义和使用点之间的关系显式串起来
- 为 DCE、常量传播、copy propagation、trivial phi elimination 提供直接查询

建议输入：

- 一个 `Function`
- 当前 IR 中所有显式 `ValueRef` use

这里第一版完全可以不依赖 dominator 或 liveness，直接扫描 IR 建表。

建议输出：

- `ValueId -> 定义者`
- `ValueId -> uses`
- `Instruction* -> used ValueRef 列表`
- `Instruction* -> defined ValueId 列表`

第一版 `Result` 可以长成：

```cpp
struct DefUseAnalysis {
    struct UseSite {
        Instruction* user = nullptr;
        std::size_t operand_index = 0;
    };

    struct Result {
        std::unordered_map<ValueId, Instruction*> def_of;
        std::unordered_map<ValueId, std::vector<UseSite>> uses_of;
        std::unordered_map<Instruction*, std::vector<ValueRef>> operands_of;
        std::unordered_map<Instruction*, std::vector<ValueId>> defs_of_inst;

        Instruction* defining_instruction(ValueId id) const;
        const std::vector<UseSite>& uses(ValueId id) const;
    };
};
```

这里的重点是：

- `DefUse` 的分析域应优先以 `ValueId / ValueRef` 为主，而不是名字字符串
- 这样后续大部分 value-based pass 都不需要重新扫描整函数找 use

### 1.2 这 5 个 analysis 的依赖关系

建议依赖关系收敛成下面这样：

- `CFGAnalysis`：无前置分析，直接读 `Function`
- `DominatorTreeAnalysis`：依赖 `CFGAnalysis`
- `DominanceFrontierAnalysis`：依赖 `CFGAnalysis + DominatorTreeAnalysis`
- `LivenessAnalysis`：依赖 `CFGAnalysis`
- `DefUseAnalysis`：无硬依赖，直接扫描 IR；必要时可读 `CFGAnalysis`

### 1.3 对 `BuildPrunedSSA` 来说真正需要的输入

从 SSA 构建的角度看，真正关键的是这组输入：

- `CFGAnalysis`
- `DominatorTree`
- `DominanceFrontier`
- `Liveness`
- “可提升的局部名字集合”

其中：

- `DominanceFrontier` 决定 `phi` 插入候选块
- `Liveness` 决定这些候选块里哪些 `phi` 真正需要保留
- `DominatorTree` 决定 rename 顺序
- `DefUse` 更多是 SSA 建好之后给优化 pass 用，但也能帮助 verifier 和调试工具做交叉检查

## 2. 指令副作用建模

做 DCE 和代码移动前，需要先明确哪些指令可能有副作用。

建议先做一个保守分类：

- 纯计算：`Number`、`UnaryOp`、`BinOp`
- 纯 SSA 合流：`Phi`
- 可能有副作用：大多数 `Call`
- 控制流终结：`CondJump`、`Jump`、`Return`

如果后续要对 builtin 做更激进优化，可以再细分：

- 纯 builtin
- 读环境 builtin
- 写环境 builtin
- 可能抛错 builtin

## 3. Def-use / use-def 信息

SSA 的很多优化都依赖 use 链。

建议提供统一接口，至少支持：

- 查询某个定义被哪些指令使用
- 查询某条指令使用了哪些定义
- 在重写操作数后增量更新 use 链

这样后续做：

- 常量传播
- DCE
- copy propagation
- CSE

都会轻松很多。

## 与优化 pass 的关系

## SSA 之前适合先做的 pass

这些 pass 可以先在当前 IR 或“近 SSA IR”上落地：

- CFG 可达性清理
- 常量折叠
- 简单 copy propagation
- 删除未引用的纯表达式
- 分支条件常量化后的块裁剪

这样可以先建立 pass pipeline、验证框架和测试习惯。

## SSA 之后更值得做的 pass

这些 pass 在 SSA 上实现通常更自然：

- 全局常量传播
- SCCP
- 基于 use 链的 DCE
- 冗余 phi 删除
- 简单 CSE / GVN
- 死块删除后的 SSA 修复

## 风险点

## 1. MATLAB-like 动态语义会限制可 SSA 化范围

例如：

- 动态名字解析
- 环境可见性变化
- 复杂对象/索引写回
- 隐式工作区交互

所以第一阶段要明确：

- SSA 不是对所有语义一刀切
- 只对“可局部静态化”的那部分 IR 做 SSA

## 2. 多返回值与输出变量语义需要谨慎处理

函数调用和函数返回都不是单纯的单结果 SSA 语言模型，设计时必须从第一天起就考虑多结果定义。

## 3. debug 可读性不能丢

如果 SSA 名称完全失去源码名字，排查问题会很痛苦。

因此文档、打印和 verifier 都应把：

- 原始名字
- SSA 版本
- 源码位置

作为一等信息保留。

## 最终建议

对 Baltam_IR，推荐结论如下：

1. 现在就开始规划 SSA，但不要一次性把执行链全部重写成 SSA。
2. 先建立 pass pipeline 和分析基础设施，再把 IR 推向 value-based。
3. 在引入 JIT 之前完成“优化 IR 的 SSA 化”，不要等 JIT 再做。
4. 长期目标可以是 SSA IR 解释器，但中间最好先经过 value-based IR 解释器阶段。
5. 第一阶段只覆盖局部变量、参数、返回值和 lowering 生成的隐藏临时量。
6. 优先让 SSA 服务于常量折叠、常量传播、DCE 和 CFG 简化。
7. 现有解释器先保留为 fallback，优化结果通过降级、回写或双轨执行逐步接入执行链。

如果用一句话概括本方案：

`SSA IR 解释器可以作为长期目标，但当前最稳妥的路线是先把 IR 推向 value-based，再逐步收敛到完整 SSA。`

## 当前代码状态小结

如果只看当前仓库代码，而不是抽象方案，可以把状态总结成：

- 还没有完整 SSA
- 已经完成了 value-based IR 的第一轮基础设施铺设
- `Call` / `Return` / 条件 / 赋值 / 一元二元表达式都已经开始显式使用 `ValueRef`
- 解释器已经具备“名字环境 + value 槽位”的双轨执行能力
- 下一阶段最关键的工作是补 analysis/verifier/BuildPrunedSSA，并把名字环境语义继续从纯局部值路径里收缩出去

## 成熟优化器的标准方案

如果目标不是“只把当前 AST lowering 变得更干净”，而是建设一个能长期承载中端优化的成熟优化器，那么更推荐采用下面这条主线：

`Canonical CFG + Pruned SSA + Incremental SSA Repair`

这里的重点不只是“第一次把程序转成 SSA”，而是围绕 SSA 建立一整套稳定的分析、验证和修复基础设施。

### 1. 前端先生成规范 CFG

前端和 lowering 的职责应尽量收敛为：

- 生成语义正确的 `BasicBlock`
- 保证每个 block 只有一个合法 terminator
- 显式维护 CFG 边
- 把控制流与副作用表达清楚

这一层不必强求一步到位生成最优 SSA，也不必把所有 SSA 逻辑都继续内嵌在 lowering 中。

### 2. 中端独立执行 BuildPrunedSSA

成熟优化器通常会把 SSA 构建做成独立 pass，而不是依赖 AST 结构化信息硬编码在 lowering 里。

标准步骤通常包括：

- 计算可达块和逆后序遍历
- 构建 dominator tree
- 计算 iterated dominance frontier
- 对真正 live 的局部变量插入 `phi`
- 沿 dominator tree 做 rename

这样得到的是适用于任意 CFG 的 `Pruned SSA`，而不是只适用于 `if` / `while` / `for` / `switch` 这些结构化控制流。

### 3. 核心优化都在 SSA 上做

典型的中端优化流水线通常会围绕 SSA 组织，例如：

- `SimplifyCFG`
- `ConstantFold`
- `SCCP`
- `InstCombine`
- `CopyProp`
- `CSE` / `GVN`
- `DCE` / `ADCE`
- `LoopSimplify`
- `LCSSA`
- `LICM`
- `IndVarSimplify`

具体顺序可以随项目规模调整，但核心思想基本一致：

- CFG 先规范化
- 数据流优化在 SSA 上做
- 循环优化在规范 loop form 上做

### 4. 改 CFG 的 pass 依赖 Incremental SSA Repair

成熟优化器的关键不在于“第一次建 SSA”，而在于后续 pass 改写 CFG 之后，不需要每次都整函数重建 SSA。

更常见的做法是：

- 拆块、合并块、边分裂后局部修复 SSA
- 增量更新 `phi`
- 删除 trivial `phi`
- 必要时局部更新 dominator 信息

也就是说，工业强度更高的不是单独的 `Pruned SSA`，而是：

- `BuildPrunedSSA`
- `RepairSSA`
- `Verifier`

三者配套工作。

### 5. 把值 SSA 和环境/内存语义分开

成熟优化器通常不会试图把所有语义都塞进同一种 SSA 里。

更合理的分层是：

- 纯局部临时值进入 Value SSA
- 工作区、动态名字绑定、可观察环境写入继续保留显式环境语义
- 如果后续要做更深的内存优化，再引入 `MemorySSA` 或等价分析

对 MATLAB-like 语言尤其如此，因为动态名字、环境交互和复杂写回语义天然不适合直接按普通局部寄存器值建模。

### 6. Verifier 是必需品

成熟优化器几乎都离不开 verifier。

至少应提供这些检查：

- `verify_cfg`
- `verify_dominance`
- `verify_ssa`
- `verify_phi_completeness`
- `verify_use_def`

开发阶段最好支持在每个 pass 后启用验证，否则 SSA 和 CFG 问题通常会在很晚才暴露。

## 对当前项目的落地建议

如果把上面的成熟方案映射到 Baltam_IR，比较稳妥的路线是：

1. 保留当前结构化 lowering，继续把它当作前端便利层使用。
2. 在 lowering 之后新增独立的 `BuildPrunedSSA`、`RepairSSA` 和 `Verifier`。
3. 让纯局部变量逐步停止依赖 `AssignInstruction` / 名字环境语义。
4. 把 `AssignInstruction`、`BindingInstruction` 和类似机制收缩到真正需要保留环境可观察性的场景。
5. 等开始做系统性的 CFG 变换优化时，再把当前“结构化直接插 phi”的逻辑逐步让位给通用 SSA 基建。

换句话说：

- 对当前阶段，结构化直接 SSA lowering 仍然合理
- 对成熟优化器终态，应演进到独立的 `Pruned SSA + Incremental Repair`

## 按成熟方案落地还需要的步骤

如果决定按上面的成熟优化器路线推进，那么后续工作重点不再是“继续在 lowering 里补更多 `phi`”，而是先把优化器基础设施搭起来。

更稳妥的主线是：

`Canonical CFG -> Analysis/Verifier -> BuildPrunedSSA -> SSA Passes -> RepairSSA`

### Phase 1：先把执行链和优化链分开

目标：

- 保持当前解释器和执行 IR 可继续工作
- 在 lowering 之后新增独立优化入口
- 避免把 SSA 改造和解释器重写绑成一次大改

建议动作：

- 新增 `optimize_module()` 或 `optimize_function()` 入口
- 建立独立 pass pipeline
- 允许 pass 前后打印 IR 和运行 verifier

这一阶段的核心不是“优化本身”，而是把后续分析和 SSA pass 放到一个稳定框架里。

### Phase 2：建立 pass manager 和目录结构

当前仓库还没有独立的 `optimizer/analysis` 模块，因此建议先补目录和骨架。

建议新增：

- `src/optimizer/pass.h`
- `src/optimizer/pass_manager.h`
- `src/optimizer/pass_manager.cpp`
- `src/optimizer/optimize.h`
- `src/optimizer/optimize.cpp`
- `src/analysis/cfg_analysis.h`
- `src/analysis/cfg_analysis.cpp`
- `src/analysis/verifier.h`
- `src/analysis/verifier.cpp`

同时需要更新构建系统，把这些新模块纳入 [CMakeLists.txt](/home/zj/Desktop/Baltam_IR/CMakeLists.txt)。

建议这一层先解决：

- pass 注册和执行顺序
- 是否打印 pass 前后 IR
- 是否在每个 pass 后自动运行 verifier

### Phase 3：先写 verifier，再写优化

如果没有 verifier，后面做 SSA 和 CFG 改写时很难快速定位错误。

第一批 verifier 建议至少覆盖：

- `verify_cfg`
- `verify_terminator`
- `verify_phi_completeness`
- `verify_value_defs`
- `verify_use_def`

建议检查内容包括：

- 每个基本块的终结指令是否合法
- CFG 前驱后继是否一致
- `phi` 的 incoming 是否与前驱块集合匹配
- 所有 `ValueRef` 是否都能找到合法定义
- 一条指令的结果值定义是否完整且无重复

这一步完成后，SSA 和 CFG 相关 bug 会更容易在开发阶段暴露，而不是拖到解释执行或后续 pass 才出问题。

### Phase 4：先把 CFG 规范化

成熟 SSA 构建通常建立在 canonical CFG 之上，因此在真正做 `BuildPrunedSSA` 之前，应补齐最基本的 CFG 工具。

建议优先提供：

- 可达块分析
- 逆后序遍历
- CFG 一致性检查
- 必要时的 critical edge split

在这一步，先不要急着做复杂优化，重点是保证：

- block 结构稳定
- terminator 规则统一
- 后续分析可以可靠地跑在所有函数上

### Phase 5：补分析层

`Pruned SSA` 不是只靠 `phi` 节点本身实现的，它依赖一组基础分析结果。

建议优先级如下：

1. reachability / reverse postorder
2. dominator tree
3. dominance frontier
4. liveness
5. def-use / use-def

其中：

- `dominator tree` 和 `dominance frontier` 决定 `phi` 插入位置
- `liveness` 决定是否做 pruned / semi-pruned
- `def-use` 决定后续 DCE、copy propagation、常量传播的可实现性

### Phase 6：明确可提升为 SSA 的对象范围

第一版不要试图把所有语义都直接做成 SSA。

建议第一阶段只覆盖：

- 形参
- 局部变量
- 返回值名
- lowering 生成的隐藏临时量

暂时不要纳入：

- 动态名字解析
- 工作区交互
- 复杂索引写回
- 全局环境可观察语义

这一步的目的，是把“普通局部值”和“环境/运行时语义”严格区分开。

### Phase 7：收缩名字环境语义

当前项目真正的结构性问题，不只是 `phi` 插入还不够系统，而是：

- 局部变量语义
- 名字环境语义
- 执行期可观察绑定语义

这三者目前还没有完全拆开。

因此在做通用 SSA 之前，建议逐步让：

- 纯局部计算更多只依赖 `ValueRef`
- `AssignInstruction` 收缩为“真正需要保留的名字写入”
- `BindingInstruction` 收缩为“真正需要保留的名字读取”

如果这一层不先做清楚，后面的通用 SSA pass 很容易被动态语义污染。

### Phase 8：实现独立的 BuildPrunedSSA

等前面的 verifier、CFG 分析、dominance、liveness 都具备后，再实现独立的 SSA 构建 pass。

这里需要明确一个原则：

- `phi` 的权威来源最好只有一个

也就是说，长期来看应逐步从“lowering 内嵌结构化 `phi`”过渡到“中端 pass 统一构建 SSA”。

第一版 `BuildPrunedSSA` 应至少完成：

- 对可提升局部变量收集定义块
- 基于 dominance frontier 插入 `phi`
- 基于支配树执行 rename
- 删除 trivial `phi`

### Phase 9：在 SSA 上落第一批优化

SSA 建好以后，建议先上收益高、风险低、验证相对直接的几类 pass：

- `SimplifyCFG`
- trivial `phi` elimination
- `DCE`
- 常量折叠
- 简单全局常量传播或 `SCCP`

不要一开始就上过重的循环优化或复杂值编号，先把 SSA 基础链跑通更重要。

### Phase 10：在确实需要时再做 RepairSSA

`RepairSSA` 是成熟优化器的重要组成部分，但不建议在项目还没有 CFG 改写 pass 时就过早实现。

更合理的顺序是：

- 先让 `BuildPrunedSSA` 和第一批 SSA pass 稳定
- 等开始做拆块、删块、边分裂、分支合并等变换时
- 再补增量 `RepairSSA`

第一版 `RepairSSA` 只需要覆盖项目真实会做的 CFG 改写，不必一开始追求泛化到所有变换。

## 当前最应该马上做的事情

如果只看当前仓库，最值得优先推进的是这 6 件事：

1. 新建 `src/optimizer/` 和 `src/analysis/`，并更新构建系统。
2. 先补 `PassManager`、`AnalysisManager` 骨架和 `optimize_module()` / `optimize_function()` 入口。
3. 先写 `Verifier`，把 CFG / phi / `ValueRef` 这几类错误尽早前置暴露。
4. 写 `CFGAnalysis`、逆后序遍历和 `DominatorTree`。
5. 写 `DominanceFrontier`、`Liveness` 和 `DefUse`。
6. 再开始 `BuildPrunedSSA` 和第一批 SSA pass。

## 当前不建议立刻做的事情

为了控制风险，下面这些事情不建议现在马上开始：

- 不要先写完整 `RepairSSA`
- 不要先把解释器重写成 SSA 解释器
- 不要继续把更多 SSA 逻辑塞进 lowering
- 不要在 verifier 还没建立好之前就做 `SCCP`、`LICM` 这类更复杂的 pass

## 合并后的实施顺序

如果把前面的建议压成一个可直接开工的路线，建议按下面的顺序推进：

### Step 1：搭目录和入口

先补下面这些文件：

- `src/optimizer/pass.h`
- `src/optimizer/pass_manager.h`
- `src/optimizer/pass_manager.cpp`
- `src/optimizer/optimize.h`
- `src/optimizer/optimize.cpp`
- `src/analysis/analysis_manager.h`
- `src/analysis/analysis_manager.cpp`
- `src/analysis/verifier.h`
- `src/analysis/verifier.cpp`

这一阶段先不追求任何优化收益，只解决两件事：

- lowering 之后能进入统一的优化入口
- pass 前后能稳定打印 IR 和跑 verifier

### Step 2：先把 verifier 变成开发主护栏

第一版 verifier 建议覆盖：

- CFG 前驱/后继一致性
- terminator 合法性
- `phi` incoming 与前驱集合匹配
- `ValueId` / `ValueRef` 的定义唯一性和引用合法性
- use-def 的基本一致性

做到这一步后，后续 analysis 和 SSA pass 出错时，问题会在开发阶段更早暴露。

### Step 3：补 analysis 基础层

建议按依赖顺序实现：

1. `CFGAnalysis`
2. `DominatorTree`
3. `DominanceFrontier`
4. `Liveness`
5. `DefUse`

这里不要一开始就做复杂缓存策略，先让每种 analysis 都能对单个 `Function` 稳定产出结果。

### Step 4：实现 `BuildPrunedSSA`

第一版只覆盖：

- 形参
- 局部变量
- 返回值名
- lowering 生成的隐藏临时名

并完成：

- 收集定义块
- 基于 `DominanceFrontier` 插 `phi`
- 基于支配树做 rename
- 删除 trivial `phi`

### Step 5：在 SSA 上落第一批优化

建议第一批只做：

- `SimplifyCFG`
- trivial `phi` elimination
- `DCE`
- `ConstantFold`

这些 pass 都比较适合作为“把链路跑通”的第一批目标。

### Step 6：开始有 CFG 改写时再补 `RepairSSA`

只有当中端开始稳定做下面这些事情时，`RepairSSA` 才值得进入主线：

- 拆块
- 合并块
- 删边 / 删块
- critical edge split

在这之前，整函数重建 SSA 往往已经够用。

## analysis 和 optimizer / PassManager 方案

### 1. 目录结构

建议采用两层目录：

- `src/analysis/`
- `src/optimizer/`

推荐的最小文件布局如下：

- `src/analysis/analysis_manager.h`
- `src/analysis/analysis_manager.cpp`
- `src/analysis/cfg_analysis.h`
- `src/analysis/cfg_analysis.cpp`
- `src/analysis/dom_tree.h`
- `src/analysis/dom_tree.cpp`
- `src/analysis/dom_frontier.h`
- `src/analysis/dom_frontier.cpp`
- `src/analysis/liveness.h`
- `src/analysis/liveness.cpp`
- `src/analysis/def_use.h`
- `src/analysis/def_use.cpp`
- `src/analysis/verifier.h`
- `src/analysis/verifier.cpp`
- `src/optimizer/pass.h`
- `src/optimizer/pass_manager.h`
- `src/optimizer/pass_manager.cpp`
- `src/optimizer/optimize.h`
- `src/optimizer/optimize.cpp`
- `src/optimizer/passes/build_pruned_ssa.h`
- `src/optimizer/passes/build_pruned_ssa.cpp`
- `src/optimizer/passes/simplify_cfg.h`
- `src/optimizer/passes/simplify_cfg.cpp`
- `src/optimizer/passes/dce.h`
- `src/optimizer/passes/dce.cpp`
- `src/optimizer/passes/constant_fold.h`
- `src/optimizer/passes/constant_fold.cpp`

第一版只做 `Function` 级别即可，不要急着引入 `ModulePassManager`。

### 2. `analysis` 层职责

`analysis` 层只做“读 IR、产出结果、不给 IR 施加副作用”的事情。

建议把它和 `optimizer` 明确分开：

- `analysis` 负责缓存和提供分析结果
- `optimizer` 负责改 IR
- `verifier` 负责判定 IR 是否仍然合法

这样 `BuildPrunedSSA`、`DCE`、`SimplifyCFG` 等 pass 都只通过统一接口取分析结果，不直接自己散落实现一份 CFG / dominator 逻辑。

### 3. `AnalysisManager` 方案

第一版建议只做 `FunctionAnalysisManager`，按函数缓存分析结果。

最小接口可以是：

```cpp
class FunctionAnalysisManager {
public:
    template <typename AnalysisT>
    const typename AnalysisT::Result& get(Function& function);

    template <typename AnalysisT>
    void invalidate(Function& function);

    void invalidate_all(Function& function);
    void clear();
};
```

每个 analysis 约定提供：

```cpp
struct CFGAnalysis {
    struct Result {
        std::vector<BasicBlock*> reverse_postorder;
        std::vector<BasicBlock*> reachable_blocks;
    };

    Result run(Function& function, FunctionAnalysisManager& am);
};
```

后续其他 analysis 也用同样模式：

- `DominatorTreeAnalysis`
- `DominanceFrontierAnalysis`
- `LivenessAnalysis`
- `DefUseAnalysis`

建议依赖关系如下：

- `DominatorTreeAnalysis` 依赖 `CFGAnalysis`
- `DominanceFrontierAnalysis` 依赖 `DominatorTreeAnalysis`
- `LivenessAnalysis` 依赖 `CFGAnalysis`
- `DefUseAnalysis` 直接扫描 IR，必要时可读 `CFGAnalysis`

第一版缓存策略可以非常保守：

- 任何会改 CFG 的 pass，直接 `invalidate_all(function)`
- 任何只改局部指令、不改 CFG 的 pass，至少失效 `DefUse` / `Liveness`

不要一开始就追求细粒度 preserved-analysis 逻辑。

### 4. `Pass` 接口方案

第一版建议只定义函数级 pass：

```cpp
struct PreservedAnalyses {
    bool preserve_all = false;
    bool preserve_cfg = false;
    bool preserve_dom_tree = false;
    bool preserve_dom_frontier = false;
    bool preserve_liveness = false;
    bool preserve_def_use = false;

    static PreservedAnalyses none();
    static PreservedAnalyses all();
};

class FunctionPass {
public:
    virtual ~FunctionPass() = default;
    virtual const char* name() const = 0;
    virtual PreservedAnalyses run(Function& function, FunctionAnalysisManager& am) = 0;
};
```

这样 pass 的责任边界很明确：

- 需要分析就从 `FunctionAnalysisManager` 取
- 改完 IR 后返回自己保留了哪些 analysis
- `PassManager` 负责据此做失效处理

### 5. `PassManager` 方案

第一版建议保守实现 `FunctionPassManager`：

```cpp
struct PassManagerOptions {
    bool verify_before_pipeline = true;
    bool verify_after_each_pass = true;
    bool print_before_each_pass = false;
    bool print_after_each_pass = false;
};

class FunctionPassManager {
public:
    void add_pass(std::unique_ptr<FunctionPass> pass);
    void run(Function& function, FunctionAnalysisManager& am,
             const PassManagerOptions& options = {});
};
```

推荐行为：

- pipeline 开始前先跑一次 verifier
- 每个 pass 前后可选打印 IR
- 每个 mutating pass 后按 `PreservedAnalyses` 失效 analysis
- 如果开启 `verify_after_each_pass`，每个 pass 后都重新 verifier

第一版可以进一步简化：

- mutating pass 一律 `invalidate_all(function)`
- 只有 `VerifierPass` 这类纯检查 pass 返回 `PreservedAnalyses::all()`

等 pass 数量和分析成本上来之后，再做细粒度 preserved-analysis。

### 6. `optimize_function()` 和 pipeline 方案

建议对外只先暴露两个入口：

```cpp
void optimize_function(Function& function);
void optimize_module(Module& module);
```

其中：

- `optimize_module()` 只负责遍历模块里的函数并调用 `optimize_function()`
- 真正的 pipeline 先收敛在 `optimize_function()` 内部

第一版 pipeline 建议如下：

1. `VerifyIRPass`
2. `CanonicalizeCFGPass` 或最小版 `SimplifyCFGPass`
3. `VerifyIRPass`
4. `BuildPrunedSSAPass`
5. `VerifyIRPass`
6. `TrivialPhiEliminationPass`
7. `DCEPass`
8. `ConstantFoldPass`
9. `SimplifyCFGPass`
10. `VerifyIRPass`

如果想再保守一点，第一版甚至可以只上：

1. `VerifyIRPass`
2. `BuildPrunedSSAPass`
3. `VerifyIRPass`

先把 SSA 主链打通，再逐步加优化 pass。

### 7. `Verifier` 在体系里的位置

建议不要把 verifier 设计成普通分析缓存结果，而是设计成一个独立工具：

- `analysis::verify_function(function)` 返回诊断列表或抛异常
- `optimizer::VerifyIRPass` 只是对这个工具的包装

这样既可以在 `PassManager` 内自动调用，也可以在 lowering 之后、调试脚本里、单元测试里直接调用。

### 8. 适合当前项目的最小落地版本

如果只做一个最小但方向正确的版本，建议收敛成下面这组组件：

- `FunctionAnalysisManager`
- `FunctionPass`
- `FunctionPassManager`
- `Verifier`
- `CFGAnalysis`
- `DominatorTreeAnalysis`
- `BuildPrunedSSAPass`

也就是说，当前最现实的最小链路是：

`lowering -> verify -> optimize_function -> BuildPrunedSSA -> verify`

这样推进的好处是：

- 每一步都有可验证产物
- 不需要一次性重写解释器
- 现有 lowering 还能继续作为稳定前端
- SSA 的引入和优化器能力建设能同步推进
