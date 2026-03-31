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
2. 加 `pass manager` 骨架和 `optimize_module()` / `optimize_function()` 入口。
3. 先写 verifier。
4. 写可达块分析、逆后序遍历和 dominator tree。
5. 写 dominance frontier、liveness 和 def-use。
6. 再开始 `BuildPrunedSSA`。

## 当前不建议立刻做的事情

为了控制风险，下面这些事情不建议现在马上开始：

- 不要先写完整 `RepairSSA`
- 不要先把解释器重写成 SSA 解释器
- 不要继续把更多 SSA 逻辑塞进 lowering
- 不要在 verifier 还没建立好之前就做 `SCCP`、`LICM` 这类更复杂的 pass

## 适合当前项目的实施顺序

如果压成最小可执行路线，可以用下面这个顺序：

1. `PassManager + Verifier`
2. `CFGAnalysis + DominatorTree`
3. `DominanceFrontier + Liveness + DefUse`
4. `BuildPrunedSSA`
5. `SimplifyCFG + DCE + ConstantFold`
6. `RepairSSA`

这样推进的好处是：

- 每一步都有可验证产物
- 不需要一次性重写解释器
- 现有 lowering 还能继续作为稳定前端
- SSA 的引入和优化器能力建设能同步推进
