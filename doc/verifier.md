# Baltam_IR Verifier 说明

当前 verifier 位于：

- [src/analysis/verifier.h](/home/zj/Desktop/Baltam_IR/src/analysis/verifier.h)
- [src/analysis/verifier.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/verifier.cpp)

它当前面向两种正式 stage：

- `NonSSA`
- `UntypedSSA`

它负责校验 `Module / Function / BasicBlock / NonSSANode / UntypedSSANode`，是当前 IR 主线里的基础结构护栏。`TypedSSA` 仍未接入。

## 当前 verifier 覆盖的检查

### 1. 容器级检查

- `Module` 必须有入口函数
- 入口函数必须属于当前模块
- `Function` 必须有入口块
- 入口块必须属于当前函数
- `BasicBlock::parent()` 必须正确

### 2. 所有 stage 共享的结构检查

- `IRNode::parent()` 必须正确
- 节点 `stage` 必须和所属 `Function::stage()` 一致
- 同一节点不能被重复挂接到多个位置
- predecessor / successor 必须双向一致
- 跳转目标块必须属于当前函数
- terminal 目标块必须出现在 successor 列表中

### 3. `NonSSA` 阶段检查

- `phi_nodes()` 区域必须为空
- 正文里只能出现 `NonSSANode`
- 正文里不能出现 terminator
- `terminal` 必须是：
  - `CondJumpNode`
  - `JumpNode`
  - `ReturnNode`
- `NonSSA` 函数不应携带 SSA 参数槽位或 SSA value table

### 4. `UntypedSSA` 阶段检查

- `phi_nodes()` 区域里只能出现 `SSAPhiNode`
- 正文里只能出现 `UntypedSSANode`
- 正文里不能出现 `phi` 或 terminator
- `terminal` 必须是：
  - `SSACondJumpNode`
  - `SSAJumpNode`
  - `SSAReturnNode`
- `phi incoming` 的前驱集合必须和块前驱列表一致
- `argument_values()` 个数必须与输入签名一致
- 每个 `ValueId` 必须要么是参数值，要么有且仅有一个定义
- `ValueRef` 必须引用函数内已知的 SSA 值
- 直接调用必须带 `direct_symbol`
- 间接调用必须带有效的 `indirect_value`
- `SSAReturnNode` 返回值个数必须与输出签名一致

## 当前 verifier 还没做的事情

当前 verifier 还没有覆盖这些更强的语义检查：

- `NonSSA` 的 must-def / use-before-def 分析
- `UntypedSSA` 上更强的 dominance 约束检查
- helper / builtin 的精细调用签名检查
- `TypedSSA` 结构与值规则
- 更高层的类型、别名或 effect 约束

这些检查仍应逐步补在：

- analysis
- 更强的 stage-specific verifier
- 后续 optimizer / typed SSA 层

## 推荐演进方式

当前更合理的方向不是再做一个“单独的 SSA verifier”，而是继续维持统一入口：

- `verify_module(...)`
- `verify_function(...)`

再按 stage 分层扩充：

- `NonSSA`
  - 补 must-def、名字级 use-before-def、helper 形状检查
- `UntypedSSA`
  - 补 dominance、effect、调用约束
- `TypedSSA`
  - 补类型一致性和 specialized 约束

## 当前结论

verifier 已经是当前 IR 主线里的基础护栏，而不是未来计划项。

它当前承担的职责是：

- 保证 `NonSSA` 和 `UntypedSSA` 的容器与节点结构合法
- 保证 SSA 值定义/使用关系的基本一致性
- 给打印、解释执行和后续优化 pass 提供一个统一的前置校验
