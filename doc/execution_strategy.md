# Matlab 执行策略与 Runtime 机制设计

## 目标

本文不再讨论 IR 设计为什么演化，而是说明在当前 IR 方向基本确定之后，执行引擎应如何分层，以及每一层最小需要哪些 runtime 机制。

这里讨论的前提结构是：

```text
AST
 ↓
IR（non-SSA，语义 IR）
 ↓
────────────────────────────
Execution Engine
────────────────────────────

Tier 0:
  Interpreter

Tier 1:
  Baseline JIT（可选，但强烈建议）

Tier 2:
  Region-based Optimizing JIT
    - hot region detection
    - region -> typed SSA
    - aggressive optimization
    - LLVM IR

Deopt:
  -> 回退到 high-level IR continuation
```

本项目不在 high-level IR 与 interpreter 之间增加独立的中间执行 IR。Tier 0 直接遍历
`CodeUnit`、`BasicBlock` 和具体 `Instruction`；profile、调试位置和 deopt continuation 也统一
锚定到这套 high-level IR 身份。

## 总体原则

执行策略层的核心目标不是重新定义语义，而是在保证语义基线统一的前提下，给不同热点阶段分配不同成本和不同收益的执行方式。

这里应坚持几个原则：

- high-level IR + interpreter 是唯一稳定语义基线
- JIT 只负责更快地执行，不负责重新定义语义
- 所有优化代码都必须能够可靠回退到 IR interpreter
- 热点编译要尽量局部化，避免把强动态边界整体吞入优化区

## 为什么采用 Tiered Execution

如果只有解释器和重优化 JIT，两者之间会有明显断层：

- 解释器启动快，但中等热点性能不足
- 重优化 JIT 性能上限高，但编译成本和失效成本更高

因此，合理的执行模型是分层执行：

- `Tier 0` 提供语义基线和 profiling
- `Tier 1` 提供低成本机器码执行
- `Tier 2` 提供真正的热点优化

这比重新发明一种新 IR 更能决定最终系统表现。

## Tier 0：Interpreter

Interpreter 是整个系统的真实语义执行器，至少承担以下职责：

- 直接执行 high-level IR
- 收集热点计数与基础 profile
- 提供 deopt 后的恢复目标
- 处理所有动态特性和慢路径

Tier 0 本身不追求最强性能，但必须保证：

- 语义完整
- 状态可观测
- 能与后续 tier 共用 profiling 数据结构

## Tier 1：Baseline JIT

### 角色

Tier 1 的目标是用较低编译成本替代解释执行，改善 warmup 和中等热点性能。它不追求最激进优化，而追求：

- 编译快
- 行为接近 IR interpreter
- 易于失效和回退

Tier 1 最自然的编译粒度是 `CodeUnit` 或函数入口，也可以只编译高频 CFG region。

### Tier 1 最小需要的 Runtime 机制

#### 1. Hotness Counter / Compile Trigger

Tier 1 至少需要一套从解释器触发编译的热点检测机制，例如：

- 函数调用计数
- 循环回边计数
- call edge 热度计数

没有这套机制，Baseline JIT 无法自动接管中等热点代码。

#### 2. Inline Cache（至少是单态 IC）

Tier 1 最小应具备基础 IC，用来降低动态分派成本。优先级最高的场景通常包括：

- 函数调用目标解析
- 内建函数或用户函数分派
- 全局符号或工作区名字解析
- 成员访问或索引辅助路径选择

对于 Matlab 这类动态环境语言，完全没有 IC，Baseline JIT 的收益会被大量运行时查找吞掉。

最低可行版本通常是：

- call-site monomorphic IC
- global/builtin lookup cache
- 与环境版本号联动的 cache invalidation

#### 3. Guard

Tier 1 虽然不做最激进优化，但仍然需要少量 guard 来保护已缓存的假设，例如：

- 调用目标未变化
- builtin 未被 shadow
- path / workspace / mex environment 未失效

如果没有 guard，IC 和缓存就不能安全复用。

#### 4. Invalidation / Epoch 机制

对于 `clear`、`addpath`、`cd`、`rehash`、`mex` 装载等操作，系统至少需要一套统一的失效机制。最简单可行的方式是：

- 维护若干 environment epoch / version counter
- 让 IC 和已编译代码记录依赖的版本
- 版本变化后触发失效、重查找或回退

这是 Matlab 场景下运行时正确性的基础设施，不只是优化设施。

#### 5. IR Continuation Map

Tier 1 至少要能把机器码执行位置映射回 high-level IR continuation。最低要求包括：

- 机器码 safepoint 或 patchpoint 对应的 `CodeUnit / BasicBlock / Instruction`
- 当前函数帧中局部变量槽位的映射信息

即使 Baseline JIT 不做复杂 deopt，也需要这套信息来支持：

- 异常处理
- 调试或诊断
- 失效后回到 IR interpreter

#### 6. Runtime Helper ABI

Tier 1 一定会频繁调用 runtime helper，因此至少需要一套稳定 ABI，用于：

- 调用解析
- 数组分配与写屏障
- 边界检查失败后的慢路径
- 通用内建函数降级

如果 helper 调用约定不稳定，Tier 1 很难长期维护。

### Tier 1 非最小但强烈建议的机制

以下机制不是 Tier 1 的绝对最小集，但尽早预留接口会更稳：

- patchpoint / safepoint 统一抽象
- 轻量级 deopt state map
- 基础 stack map 或 GC root map
- loop entry OSR hook

其中 `OSR` 不是 Tier 1 启动所必需，但如果没有它，热点循环只能在下次进入时才切到机器码，效果会打折。

## Tier 2：Region-based Optimizing JIT

### 角色

Tier 2 是主优化层，目标是在热点稳定区域上做真正激进的专门化优化。

推荐采用 region-based，而不是把整个函数一口气编译掉。这样更适合 Matlab 的动态边界特征，因为可以：

- 避开 `eval`、`clear`、`mex`、`addpath` 等强动态点
- 聚焦数值密集和类型趋稳区域
- 更自然地组织 guard、deopt 和 OSR

Tier 2 的核心流程通常是：

- 从 profile 中识别 hot region
- 抽取 region
- 将 region 转换成 typed SSA
- 执行激进优化
- 生成 LLVM IR 和机器码

### Tier 2 最小需要的 Runtime 机制

#### 1. Profile Feedback Pipeline

Tier 2 的优化必须建立在 profile 反馈上，因此至少需要：

- 类型反馈
- 调用目标稳定性反馈
- 分支概率
- 循环热度
- 关键对象或数组形状信息

对 Matlab 来说，只有类型反馈通常不够，还需要至少部分 shape / dispatch 信息。

#### 2. Hot Region Detection

Tier 2 必须知道“编译哪里”。因此至少需要一套 region 选择机制，例如：

- hot loop
- hot path through a CFG region
- hot call edge 附近的稳定片段

没有 region 选择，优化器就只能退化成 method-based 粗粒度编译。

#### 3. Guard System

Tier 2 必须大量依赖 guard 来保护投机假设。最低要能表达并执行以下类型的 guard：

- 类型 guard
- shape / layout guard
- 调用目标 guard
- environment epoch guard
- builtin / symbol resolution guard

没有 guard，typed SSA 的大部分优化前提都不成立。

#### 4. Deopt State Map

这是 Tier 2 的最低核心机制之一。

一旦 guard 失败，优化代码必须能够回退到 IR interpreter 或较低 tier，因此需要记录：

- deopt 点对应的 IR continuation
- 当前逻辑帧与调用栈的重建方式
- live SSA value 如何 materialize 成解释器可见值
- spill slot、常量、重命名局部变量之间的映射

没有 deopt state map，Tier 2 的 guard 基本无法安全落地。

#### 5. OSR

Tier 2 最低应支持 loop OSR，至少包括：

- 从解释器进入已编译热点循环
- 必要时从优化代码退回 IR interpreter

如果没有 OSR，很多真正热的循环无法在当前执行中受益，系统会严重依赖“下一次调用再优化”。

对于数值循环很重的 Matlab 场景，OSR 基本不是可有可无的功能。

#### 6. Invalidation / Dependency Tracking

Tier 2 的代码比 Tier 1 更投机，因此需要更明确的依赖跟踪。最低至少应能跟踪：

- path / cwd 相关环境版本
- workspace / symbol table 版本
- builtin shadowing 状态
- mex 相关外部代码装载状态

当这些依赖变化时，系统必须能够：

- 失效对应机器码
- 清空相关 IC
- 让后续执行自动回退到 IR interpreter 或低 tier

#### 7. Safepoint / Stack Map

只要 Tier 2 开始跨 helper、跨调用、跨分配点做优化，就需要较正规的 safepoint 和 stack map 机制，用于：

- GC root 枚举
- 异常回退
- 异步中断
- deopt 时状态恢复

这部分在工程上很容易被低估，但它是优化运行时的基础设施。

### Tier 2 非最小但很快会需要的机制

以下机制不一定是第一版 Tier 2 的启动门槛，但很快会变成瓶颈：

- polymorphic inline cache
- call-site specialization
- escape analysis 相关 runtime 协作
- code versioning
- background compilation
- OSR exit 的更细粒度状态重建

## Deopt 设计原则

无论是 Tier 1 还是 Tier 2，都应遵循同一条原则：

- deopt 的语义目标统一回到 high-level IR continuation

这样做有几个直接好处：

- 语义基线唯一
- 恢复逻辑不会分叉
- 高 tier 不需要携带完整解释语义

因此推荐的层间关系是：

- Interpreter 是唯一完整语义执行器
- Baseline JIT 是低成本机器码层
- Optimizing JIT 是投机执行层
- 所有失败路径最终都能回到 IR interpreter

## 建议的最小落地顺序

如果按工程风险排序，比较稳的顺序是：

1. 先把 Tier 0 的 IR interpreter、profile 和环境版本机制打稳。
2. 再做 Tier 1，优先补齐 hotness counter、单态 IC、guard、IR continuation map。
3. 之后实现 Tier 2 的 hot region detection、typed SSA lowering、guard、deopt state map。
4. 最后补强 OSR、PIC、后台编译和更细粒度 invalidation。

这个顺序的原因是：

- 没有稳定的语义基线，后续 tier 都不可靠
- 没有最小 IC 和 invalidation，Tier 1 的收益不稳定
- 没有 deopt state map 和 OSR，Tier 2 的投机优化难以真正落地

## 当前建议

当前更推荐的执行模型可以概括为：

- `Tier 0`: Interpreter
- `Tier 1`: Baseline JIT
- `Tier 2`: Region-based Optimizing JIT
- `Deopt`: 统一回退到 IR interpreter

其中最关键的 runtime 机制是：

- `Tier 1`: hotness counter、单态 IC、guard、environment epoch、IR continuation map
- `Tier 2`: profile feedback、hot region detection、guard、deopt state map、OSR、dependency tracking

这套分工的核心目标很明确：

让解释器承载语义，让 Baseline JIT 承载低成本提速，让 Region-based Optimizing JIT 承载真正的投机优化。

## 相关文档

- [ir_draft.md](./ir_draft.md)
- [ir_schema.md](./ir_schema.md)
- [workspace_design.md](./workspace_design.md)
- [runtime_execution_objects_design.md](./runtime_execution_objects_design.md)
- [deopt_runtime_references.md](./deopt_runtime_references.md)
