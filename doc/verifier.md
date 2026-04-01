# Baltam_IR Verifier 说明

## 当前状态

当前 verifier 位于：

- [src/analysis/verifier.h](/home/zj/Desktop/Baltam_IR/src/analysis/verifier.h)
- [src/analysis/verifier.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/verifier.cpp)

它现在面向的是当前唯一正式 IR：

- `Module`
- `Function`
- `BasicBlock`
- `NonSSANode`

也就是说，verifier 现在已经不再校验旧 hybrid/value-based IR。

## 当前主线中的位置

当前 CLI 主线是：

`parse -> lower(non-SSA) -> print`

当前 verifier 已具备独立入口，但还没有默认接进 `main.cpp`。

后续更合理的主线应是：

`parse -> lower(non-SSA) -> verify -> print`

## 当前 verifier 覆盖的检查

当前 verifier 是“结构合法性”的第一版，不做复杂数据流分析。

它当前主要检查：

- `Module` 必须有入口函数
- 入口函数必须属于当前模块
- `Function` 必须有入口块
- 入口块必须属于当前函数
- `BasicBlock::parent()` 必须正确
- `NonSSANode::parent()` 必须正确
- 每个 block 都必须有 `terminal`
- 正文里不能出现 terminator 节点
- `terminal` 必须是：
  - `CondJump`
  - `Jump`
  - `Return`
- predecessor / successor 必须双向一致
- jump / condjump 的目标块必须属于当前函数
- `terminal` 的目标块必须出现在 successor 列表中

## 当前 verifier 还没做的事情

当前 verifier 还没有做这些更强的检查：

- must-def 分析
- 名字 live-in / live-out 分析
- `NamedValue` 级 use-before-def 检查
- helper 调用形状检查
- 输入输出个数与所有 return 语句的一致性检查

这些检查应放在后续 analysis 层稳定后再补。

## 推荐演进顺序

建议把 verifier 分成两层来做：

### 第一层：结构 verifier

这就是当前已经落地的部分。

重点是：

- CFG
- parent
- terminator
- 入口归属

### 第二层：数据流 verifier

后续补上：

- 名字 must-def
- `CallNode` 输入是否已定义
- `CondJumpNode` 条件是否已定义
- `ReturnNode` 返回值是否已定义

这层会依赖：

- CFGAnalysis
- Liveness 或等价的数据流结果

## 和后续 SSA verifier 的关系

后续引入 SSA 以后，verifier 仍然应保留统一入口，但检查内容会分阶段变化：

- non-SSA 阶段：
  - 重点查 CFG 和 must-def
- SSA 阶段：
  - 重点查 CFG、def-use、phi、dominance 相关约束

也就是说，verifier 不应被看成“只属于 SSA”的组件，而是所有 IR 阶段的统一正确性护栏。
