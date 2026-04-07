# Baltam_IR IR 设计说明

## 当前结论

当前仓库只保留一套正式 IR 定义：

- [src/ir/ir.h](/home/zj/Desktop/Baltam_IR/src/ir/ir.h)
- [src/ir/ir.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir.cpp)

这套 IR 已经同时承载：

- `NonSSA`
- `UntypedSSA`

旧 hybrid/value-based IR 已不再参与当前主线。

## IR 分层

当前类型层次如下：

- `IRNode`
  - 所有阶段共享的公共基类
- `NonSSANode`
  - non-SSA 节点基类
- `SSANode`
  - SSA 节点公共基类
- `UntypedSSANode`
  - 当前已落地的 SSA 节点基类

`IRNode` 当前保留的公共信息是：

- `stage`
- `parent`
- `source_location`

当前 `stage` 可能取值：

- `NonSSA`
- `UntypedSSA`
- `TypedSSA`

其中当前实际启用的是前两种。

## 当前两个已启用的 stage

### 1. `NonSSA`

当前 `NonSSA` 使用：

- `NamedValue`

来表示源码变量和 lowering 临时量。

它的特点是：

- 同一个名字可以多次定义
- 节点是线性语句式的
- 是 AST lowering 的直接输出

当前 non-SSA 节点集合包括：

- `NumberNode`
- `TextNode`
- `AssignNode`
- `UnaryOpNode`
- `BinOpNode`
- `CallNode`
- `CondJumpNode`
- `JumpNode`
- `ReturnNode`

### 2. `UntypedSSA`

当前 `UntypedSSA` 使用：

- `ValueId`
- `ValueRef`

来表示显式值流。

它的特点是：

- 每个 SSA 结果有独立定义点
- `phi` 独立放在块头
- 是当前统一的 SSA 打印和执行对象

当前 untyped SSA 节点集合包括：

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

## CFG 容器

当前容器层仍然统一为：

- `Module`
- `Function`
- `BasicBlock`

这三层在 `NonSSA` 和 `UntypedSSA` 之间直接复用。

### `BasicBlock`

当前 `BasicBlock` 持有：

- `phi_nodes`
- `instructions`
- `terminal`
- `predecessors`
- `successors`

约束：

- `phi` 只能出现在 `phi_nodes()`
- terminator 只能出现在 `terminal()`
- CFG 边必须双向一致

### `Function`

当前 `Function` 持有：

- 名字
- 类型
- 输入和输出签名
- `stage`
- SSA 参数槽位与 value table
- 基本块存储
- 节点存储
- 入口块

### `Module`

当前 `Module` 持有：

- 模块名
- 源文件路径
- 模块类型
- 函数列表
- 入口函数

## 当前打印语义

当前打印器位于：

- [src/ir/ir_printer.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir_printer.cpp)

打印风格仍然接近 LLVM IR，但现在已经同时支持：

- non-SSA IR
- untyped SSA IR

当前会输出：

- module / function 头
- block label
- predecessor / successor 注释
- 对齐后的源码注释
- SSA 值名和 `phi` 信息

## 当前阶段边界

当前职责划分已经明确：

- [src/lowering/lowering.cpp](/home/zj/Desktop/Baltam_IR/src/lowering/lowering.cpp)
  - 只负责 `AST -> NonSSA`
- [src/optimizer/construct_untyped_ssa.cpp](/home/zj/Desktop/Baltam_IR/src/optimizer/construct_untyped_ssa.cpp)
  - 负责 `NonSSA -> UntypedSSA`
- [src/interpreter/interpreter.cpp](/home/zj/Desktop/Baltam_IR/src/interpreter/interpreter.cpp)
  - 当前只执行 `UntypedSSA`

这意味着当前 lowering 不负责：

- SSA rename
- `phi` 插入
- type specialization
- LLVM lowering

## 后续设计边界

后续路线仍然应明确分成三层：

1. `NonSSA`
2. `UntypedSSA`
3. `TypedSSA`

其中当前第 1 层和第 2 层已经落地，第 3 层仍待设计。

后续如果进入 `TypedSSA`，应继续保持：

- 容器层尽量复用
- 节点语义单独定义
- verifier / printer / interpreter / optimizer 按 stage 明确分层
