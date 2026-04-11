# Baltam_IR UntypedSSA 优化 Pass 规划

## 当前结论

当前更合适的优化主战场是：

`UntypedSSA`

而不是直接跳到 `TypedSSA` 或 LLVM IR。

原因很直接：

- `UntypedSSA` 已经是当前统一的执行和打印对象
- 解释器已经能作为语义回归基线
- 当前 5 个基础 analysis 主要仍服务于 `NonSSA -> UntypedSSA` 构建
- `TypedSSA`、LLVM IR lowering 和 JIT runtime 还没有正式落地

因此当前阶段最值得推进的是：

1. 继续扩充 `UntypedSSA` 解释器和端到端回归
2. 在 `UntypedSSA` 上增加类型无关、结构导向的优化 pass
3. 在此基础上再设计 profile、`TypedSSA` 和 LLVM IR 路线

## 当前 `UntypedSSA` 上适合优先优化的对象

当前 `UntypedSSA` 的核心节点包括：

- `SSANumberNode`
- `SSATextNode`
- `SSAUndefNode`
- `SSAPhiNode`
- `SSACopyNode`
- `SSAUnaryOpNode`
- `SSABinOpNode`
- `SSACallNode`
- `SSACondJumpNode`
- `SSAJumpNode`
- `SSAReturnNode`

其中最适合第一批 pass 直接处理的是：

- 常量节点
- `phi`
- `copy`
- 一元 / 二元纯算术节点
- 条件跳转和无条件跳转

`SSACallNode` 也可以参与优化，但要更保守：

- 第一版默认把 call 当成“未知 effect”
- 只有进入 allowlist 的 direct helper / builtin 才参与折叠、CSE 或删除
- indirect call 第一版不参与这类优化

## 设计原则

第一批 `UntypedSSA` pass 应遵循这些原则：

- 先做类型无关优化，不依赖 `TypedSSA`
- 先做局部和 CFG 结构优化，再做更全局的数据流优化
- 先做 cleanup pass，再做需要更强前置条件的 pass
- 默认保守处理 `SSACallNode`
- 让解释器回归测试始终作为优化后的语义基线

另外需要注意：

- 当前已有的 `CFG / Dominator / DominanceFrontier / Liveness / DefUse` 主要运行在 `NonSSA`
- 因此第一批 `UntypedSSA` pass 应尽量少依赖新的 stage-specific analysis
- 真正依赖 loop、effect、dominance 的全局优化，可以在后续逐步补分析基础设施

## 推荐的 Pass 分层

### Layer 1：立即适合做的 Cleanup / Canonicalization Pass

这些 pass 最适合作为第一批正式落地对象。

#### 1. `SimplifyCFG`

职责：

- 常量条件分支折叠
- 删除不可达块
- 删除空跳板块
- 合并单前驱单后继块
- 维护并裁剪 `phi incoming`

价值：

- 直接减少基本块和跳转数量
- 暴露更多 DCE、copy propagation、常量传播机会
- 给后续优化提供更规整的 CFG

#### 2. `CopyPropagation`

职责：

- 把 `%x = copy %y` 的 uses 改写成 `%y`
- 配合 `phi` 退化和 CFG 简化继续消除中间值

价值：

- `SSACopyNode` 在当前 IR 中非常高频
- 能显著减少无意义值流节点
- 实现成本低，且与 DCE、ConstantFold 协同效果很好

#### 3. `ConstantFold`

职责：

- 折叠纯常量的一元 / 二元运算
- 折叠一部分纯 direct helper

第一批高收益目标应优先覆盖：

- 常量 `SSAUnaryOpNode`
- 常量 `SSABinOpNode`
- 常量 `horzcat / vertcat`
- 常量 `colon`

价值：

- 直接暴露死分支和不可达块
- 给 DCE、SimplifyCFG、SCCP 提供输入

#### 4. `PhiSimplify`

职责：

- 删除只有一个 incoming 的 `phi`
- 删除所有 incoming 都相同的 `phi`
- 在删边后同步收缩 `phi`

价值：

- 与 CFG 简化天然耦合
- 经常直接退化成 `copy`
- 继续配合 copy propagation 和 DCE 清理块头

#### 5. `DCE`

职责：

- 删除无 uses 且无副作用的节点

第一版最适合删除的对象：

- `SSANumberNode`
- `SSATextNode`
- `SSAUndefNode`
- `SSACopyNode`
- `SSAUnaryOpNode`
- `SSABinOpNode`
- 一部分已知纯 direct helper call

第一版不建议默认删除：

- 未建模 effect 的 `SSACallNode`
- 所有 terminator
- 仍被 `phi` / `ret` / `call` 引用的节点

价值：

- 回收前面几个 pass 暴露出来的垃圾节点
- 保持 IR 规模可控

### Layer 2：适合在第一批 Cleanup Pass 稳定后再做的数据流优化

#### 6. `SCCP`

职责：

- 同时做常量传播和可执行边分析
- 传播常量 lattice
- 发现死分支
- 发现不可达块

价值：

- 对 SSA + CFG 形式非常自然
- 往往比单纯 local ConstantFold 更有收益

前置条件：

- `SimplifyCFG`
- `ConstantFold`
- 基本稳定的 `phi` 简化和 DCE

#### 7. `GVN / CSE`

职责：

- 消除重复的纯表达式
- 合并 value-number 等价的定义

第一版建议只覆盖：

- 常量节点
- `SSAUnaryOpNode`
- `SSABinOpNode`
- 进入 purity allowlist 的 direct helper

第一版不建议覆盖：

- indirect call
- 未建模 effect 的 direct call
- 依赖内存、工作区或隐藏状态的 runtime helper

价值：

- 与 copy propagation、DCE 形成很强协同
- 能显著减少重复计算

### Layer 3：需要补更多基础设施后再做的循环优化

#### 8. `LoopSimplify` / 循环规范化

职责：

- 识别回边和循环头
- 生成 / 维护 preheader
- 规范化 latch / exit 结构

这不是最终收益 pass，而是后续循环优化的前置工作。

#### 9. `LICM`

职责：

- 把循环不变量外提到 preheader

适合外提的对象通常包括：

- 循环内重复出现、且操作数都 loop-invariant 的纯 unary / binop
- 进入 purity allowlist 的 pure direct helper

前置条件：

- loop analysis
- preheader 规范化
- 至少粗粒度的 effect / purity 模型
- 较可靠的 dominance 信息

#### 10. `Strength Reduction` / 简单循环归纳变量优化

这类优化理论上也可以在 `UntypedSSA` 做，但优先级明显低于前面的 cleanup、SCCP、GVN。

### Layer 4：当前不建议优先做的优化

#### `Loop Unrolling`

理论上可以做，但当前阶段不建议优先投入。

原因：

- 还没有成熟的 loop canonicalization
- 还没有 profile 驱动的 cost model
- 当前 `UntypedSSA` 仍保留较多 generic runtime 边界
- 很容易引起 IR 膨胀，而收益不稳定

#### 激进 `Inlining`

也不建议在当前阶段优先做。

原因：

- 解释器和回归测试仍在扩展
- call 语义和 runtime effect 还没有收敛到可放心大规模内联的程度
- 很容易把 IR 规模放大，增加调试难度

## 推荐的第一版 Pipeline 顺序

当前最合理的第一版 `UntypedSSA` pipeline 是：

### Pipeline A：基础 Cleanup 回路

1. `SimplifyCFG`
2. `CopyPropagation`
3. `ConstantFold`
4. `PhiSimplify`
5. `DCE`

这一组 pass 建议：

- 反复迭代到 fixed point
- 或至少跑 2 轮

原因是：

- CFG 简化会暴露 `phi` 退化机会
- `phi` 退化会暴露 `copy`
- copy propagation 会暴露新的常量传播机会
- 常量传播又会继续制造死分支和死节点

也就是说，这五个 pass 更像一个 cleanup cluster，而不是一次性线性执行完就结束。

### Pipeline B：稀疏数据流优化

在 Pipeline A 基本稳定之后，建议加入：

6. `SCCP`
7. `SimplifyCFG`
8. `CopyPropagation`
9. `DCE`

原因：

- `SCCP` 经常会制造新的死边和不可达块
- 这些结果需要 cleanup cluster 再清一轮

### Pipeline C：纯表达式全局消除

在 purity allowlist 和必要 analysis 到位后，可以加入：

10. `GVN / CSE`
11. `CopyPropagation`
12. `DCE`
13. `SimplifyCFG`

原因：

- CSE / GVN 合并值后，往往会留下更多 copy 和死节点
- 仍需要 cleanup cluster 做收尾

## 当前推荐的里程碑顺序

### Milestone 1

先落地：

- `SimplifyCFG`
- `ConstantFold`
- `DCE`

这是当前最小闭环，也是最容易和解释器回归测试形成正反馈的一组 pass。

### Milestone 2

再补：

- `CopyPropagation`
- `PhiSimplify`

这能明显提升第一批 pass 的整体收益。

### Milestone 3

然后落：

- `SCCP`

这通常是第一批真正“像编译器中端”的全局优化之一。

### Milestone 4

等 purity / effect 建模稍微稳定之后，再做：

- 第一版 `GVN / CSE`

### Milestone 5

等 loop analysis、preheader、effect 基础设施到位之后，再进入：

- `LoopSimplify`
- `LICM`

### Milestone 6

最后才考虑：

- `Loop Unrolling`
- 更激进的 loop 级 cost-based 优化

## 建议的实现顺序

如果按“收益 / 风险 / 实现成本”综合排序，当前建议的实际落地顺序是：

1. `SimplifyCFG`
2. `ConstantFold`
3. `DCE`
4. `CopyPropagation`
5. `PhiSimplify`
6. `SCCP`
7. `GVN / CSE`
8. `LoopSimplify`
9. `LICM`
10. `Loop Unrolling`

## 当前不建议做的事

- 不要把第一批 `UntypedSSA` 优化建立在 `TypedSSA` 前提上
- 不要默认所有 `SSACallNode` 都是纯函数
- 不要在还没有 loop / effect / cost model 时先做 `Loop Unrolling`
- 不要为了做全局优化而提前把大量复杂度推到 LLVM IR

## 当前结论

`UntypedSSA` 阶段最值得优先做的优化，不是“类型专门化”，而是：

- CFG 清理
- copy / phi 清理
- 常量传播与常量折叠
- 死代码和不可达代码消除
- 在此基础上的稀疏数据流优化与有限的纯表达式全局消除

更具体地说，近期最推荐的 pipeline 形态是：

`SimplifyCFG -> CopyPropagation -> ConstantFold -> PhiSimplify -> DCE`

先把这组 pass 做稳，再进入 `SCCP`、`GVN/CSE` 和后续 loop 优化，会更符合当前仓库的阶段边界和工程节奏。
