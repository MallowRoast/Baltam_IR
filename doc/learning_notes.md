# 后续需要学习与确认的问题

这份笔记专门用来记录：当前还没有形成稳定结论、但后续明显值得系统学习、查资料或专门做实验验证的问题。

它和 [planning_notes.md](./planning_notes.md) 的区别是：

- `planning_notes.md` 更偏“当前设计判断、实现前提和 pass 规划”
- 这份文档更偏“还有哪些知识点需要补课、查清、做实验”

如果要看已经比较成型的具体设计，可以回到 [planning_notes.md](./planning_notes.md)；如果要看 `ValueId` 类型事实、调用分派和后续 `typed SSA` 之间的专门讨论，可以继续看 [value_type_dispatch_design.md](./value_type_dispatch_design.md)。

## 使用方式

后续新增条目时，尽量补这三类信息：

- 这个问题具体要搞清什么
- 它为什么会影响当前设计或优化 pass
- 如果已经知道一些关键词、论文方向或实现术语，也顺手记下来

## 当前条目

### 1. `eval / evalin / assignin / clear` 的精确语义边界

当前已经知道这类操作会破坏名字解析稳定性和 slot / SSA 假设，但还需要更系统地确认：

- `eval(...)` 对当前函数 frame 的精确影响
- `evalin('caller', expr)` 对 caller frame 的影响边界
- `evalin('base', expr)` 与 `evalin('caller', expr)` 的区别
- `assignin('caller', ...)` / `assignin('base', ...)` 的效果差异
- `clear` 到底会失效哪些名字、哪些缓存和哪些优化假设

这直接影响：

- 哪些 region 必须视为 SSA barrier
- 哪些 call 需要打上 `MayTouchCallerFrame` 或类似 effect summary
- 哪些优化只能在 guard 成立时启用

### 2. Matlab 风格动态环境下的 effect summary 设计

需要继续学习和拆清的问题包括：

- 如何给函数打 `MayTouchOwnEnv`
- 如何打 `MayTouchCallerFrame`
- 如何打 `MayMutateWorld`
- 遇到 `apply`、函数句柄调用、未知 builtin 时如何保守传播

这会直接影响：

- 函数内 slot 提升是否能跨 call
- 调用缓存、内联和 typed SSA 的安全边界
- world / workspace epoch 该如何分层

这部分还特别值得参考 Julia 的 `world age` 设计。它讨论的是：

- 在存在 `eval` 或运行时新增 / 替换方法定义时，如何隔离已优化代码看到的“世界”
- 如何在动态语言里仍然保留一部分稳定的调用分派、内联和优化前提
- 为什么“世界计数 / world age”可以成为优化与动态语义之间的折中机制

参考：

- `World age in Julia: Optimizing method dispatch in the presence of eval`
  DOI: https://dl.acm.org/doi/10.1145/3428275

### 3. `load_slot / store_slot` 到寄存器化 / SSA 提升的经典做法

后续需要系统复习和对照实现的点：

- 单 basic block 的局部 load/store 消除
- 跨 basic block 的 mem2reg / SSA promotion
- `phi` 插入、rename、支配边界
- live-in / live-out 和 block 合流处理
- 何时需要把 SSA value materialize 回 slot

这会直接影响：

- Pass 4 的落地顺序
- 内联后清理 pass 的实现方式
- 后续 typed SSA region 的构造成本

### 4. Matlab 对象更新、索引更新与别名问题

还需要专门学习和确认：

- `a(i) = ...`
- `a.x = ...`
- cell / struct / object 的更新语义
- copy-on-write 与逻辑值语义、物理存储更新之间的关系

这决定：

- 哪些 slot 可以安全寄存器化
- 哪些写入必须视为对象级 barrier
- 哪些优化需要更细的 heap / object effect 建模

### 5. 运算符与 builtin 分派的稳定性条件

还需要继续梳理：

- `plus`、`minus`、`uminus` 等运算符函数在 Matlab 中的查找优先级
- local / private / import / path / class method 的竞争顺序
- builtin 调用在什么条件下可以视为稳定目标
- 类型事实能在多大程度上帮助提前收敛分派

这会影响：

- `BinaryInst` / `UnaryInst` 何时可以进一步收敛
- 内联后表达式能否继续做静态化
- inline cache 和 guarded exact dispatch 的设计

### 6. typed SSA、guard、deopt 的最小闭环

后续值得专门补课的点包括：

- region-based typed SSA 的最小入口条件
- guard 失败后的回退策略
- SSA value 如何映射回解释器 / bytecode 可见状态
- deopt state map 需要记录哪些最小信息

这会影响：

- 当前 non-SSA IR 和后续优化 IR 的边界
- 为什么不应该一开始就把所有主 IR 改成全局 SSA
- 哪些优化适合放在高层 IR，哪些必须留给热点优化层

### 7. 多面体编译技术的使用场景

这部分后续值得专门补课，但当前可以先记住一个很实用的高层判断：

- 多面体编译技术最适合处理规则、结构化、可静态分析的循环与数组访问
- 典型对象是数值计算里的 loop nest，尤其是 ML / HPC kernel、矩阵或张量计算一类代码
- 它特别适合“想同时优化并行性和局部性”的场景，例如 loop interchange、fusion、tiling、并行化、向量化
- 也常用于分析 cache / locality 问题、做依赖分析，或者作为硬件设计、硬件加速器代码生成的基础
- 如果代码里动态控制流、动态名字语义、`eval`、复杂 alias 或大量非仿射访问过多，多面体方法通常就不再是最自然的主工具

如果把它压缩成一句话，可以记成：

> 多面体编译技术主要用于“规则循环 + 规则数组访问 + 明确性能目标”的程序区域，尤其适合数值密集、循环密集、希望系统做并行化和局部性优化的代码。

参考：

- `FPL: Fast Presburger arithmetic through transprecision`
  DOI: https://dl.acm.org/doi/10.1145/3485539
- Polyhedral Compilation community overview: https://polyhedral.info/
- 补充阅读（知乎）: https://www.zhihu.com/question/665324252/answer/2029157784671789854?share_code=wSyZjsUWD2Po&utm_psn=2029576252382560701

### 8. MLIR，尤其是 `scf` 结构化控制流

这部分后续值得补课的重点，不只是“了解 MLIR 长什么样”，而是要看清：

- MLIR 如何把结构化控制流表示成 region-based IR
- `scf` 和普通 CFG 风格控制流之间的边界
- loop-carried value、`yield`、region argument 这类机制，如何替代传统 SSA + block parameter 的一部分组织方式

当前最值得先看的点包括：

- `scf` dialect 用来表达结构化控制流，例如 `if`、`for`、`while`、`parallel`
- `scf.for` / `scf.while` / `scf.if` 都是 region-based 的 structured control flow，而不是直接暴露 `br` / `cond_br`
- `scf.yield` 用来把 region 内的值显式传回外层 op，这对 loop-carried state、条件分支结果和结构化 lowering 很关键
- `scf` 常被当作中间 lowering 层：高层 loop / tensor / affine 结构先收敛到 `scf`，再继续 lower 到更低层的 CFG
- 官方文档明确把 `scf` 和 `cf` 区分开：`scf` 是 structured control flow，`cf` 是 branch-based、非结构化控制流

如果把它压缩成一句话，可以记成：

> 如果后续想认真设计“高层结构化 IR -> 低层 CFG / bytecode / machine IR”的分层，MLIR 的 `scf` dialect 是非常值得参考的现成范例。

后续特别值得对照学习的主题：

- `scf.for` 的 `iter_args` 与 loop-carried values
- `scf.if` / `scf.index_switch` 的结果值传递方式
- `scf.while` 的 before/after region 组织
- `scf.parallel` / `scf.forall` 对并行结构化控制流的表达
- `scf -> cf` lowering 何时发生、会丢掉哪些结构信息

参考：

- MLIR `scf` dialect 官方文档: https://mlir.llvm.org/docs/Dialects/SCFDialect/
- MLIR `cf` dialect 官方文档: https://mlir.llvm.org/docs/Dialects/ControlFlowDialect/
- MLIR Passes 文档，`-convert-scf-to-cf`: https://mlir.llvm.org/docs/Passes/

### 9. 全国大学生计算机系统能力大赛编译系统挑战赛资料

这条更适合作为长期训练资料入口，而不是单篇文档。

当前值得记住的用途是：

- 找历年赛题、赛制说明和技术方案
- 看训练资料、培训课件和比赛导向下的典型工程问题
- 观察真实竞赛环境里，大家更关注哪些编译器能力
  例如前端正确性、IR 设计、优化、代码生成、性能调优、工程组织
- 用它补“论文视角之外”的工程训练材料

如果把它压缩成一句话，可以记成：

> 这是一个很适合持续翻的竞赛资料入口，价值不只在赛题本身，更在于它能提供一批面向真实编译系统实现与优化训练的历年材料。

参考：

- 编译系统挑战赛官网: https://compiler.educg.net/#/
- 赛事聚合页: https://course.educg.net/pages/contest/index.jsp?contestCID=0&my=false&pageNo=1&stat=0

### 10. McVM / McJIT 之前的 Matlab VM 与 JIT 设计资料

这部分很值得作为“前人做过什么”的直接参考，尤其适合对照我们当前在想的这些问题：

- Matlab 这类动态科学计算语言，为什么适合做 mixed-mode 的解释器 + JIT
- 如何在动态语言里做基于实参运行时类型的函数特化
- 如何把类型 / shape inference 放进 JIT，而不是只依赖纯静态编译
- 当 JIT 还不能覆盖全部语义时，如何依赖解释器做 fallback

当前先记住几个高层点：

- McVM 的定位就是 Matlab 虚拟机，里面同时有 interpreter 和 optimizing JIT
- 它把“基于实参类型的函数版本化 / specialization”当作核心优化手段之一
- 它强调 type and shape inference，因为 Matlab 的性能问题很大一部分来自动态类型和数组 shape
- 它的架构上很值得参考的一点是：JIT 不需要一开始覆盖所有语义，可以先让解释器兜底，再逐步扩大可编译子集

如果把它压缩成一句话，可以记成：

> McVM / McJIT 很适合作为“Matlab 风格动态语言如何做 VM、解释器回退、JIT 特化与类型形状分析”的早期系统级参考。

后续值得专门看清的点：

- mixed-mode architecture 的具体分层
- function specialization / versioning system
- type and shape inference 在 JIT 场景下如何做轻量化
- 哪些优化放在 specialized function 上最划算
- 如何处理 Matlab 的动态加载、脚本 / 函数混合和复杂数组语义

参考：

- McLab 项目页 `McVM & McJIT`: https://www.sable.mcgill.ca/mclab/projects/mcvm/
- `Optimizing Matlab through Just-In-Time Specialization`
  PDF: https://www.sable.mcgill.ca/publications/papers/2010-3/mcvmcc2010.pdf

## 以后可以继续补的方向

- Matlab / Octave 官方文档与实际行为差异
- 适合参考的解释器 / JIT 实现
- SSA、effect system、abstract interpretation 相关教材或论文
- 能快速暴露语义边界的最小实验样例
