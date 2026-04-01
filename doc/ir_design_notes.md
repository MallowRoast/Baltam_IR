# Baltam_IR IR 设计说明

## 当前结论

当前仓库只保留一套正式 IR 定义：

- [src/ir/ir.h](/home/zj/Desktop/Baltam_IR/src/ir/ir.h)
- [src/ir/ir.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir.cpp)

这套 IR 当前是 non-SSA IR，不再是旧的 hybrid/value-based IR。

## IR 分层

当前类型层次如下：

- `IRNode`
  - 所有阶段共享的公共基类
- `NonSSANode`
  - 当前实际使用的节点基类
- `SSANode`
  - 预留给后续 `untyped SSA` / `typed SSA`

`IRNode` 当前保留的公共信息只有：

- `stage`
- `parent`
- `source_location`

这里的 `stage` 目前可能取值：

- `NonSSA`
- `UntypedSSA`
- `TypedSSA`

但当前实际只使用 `NonSSA`。

## NamedValue

当前 non-SSA IR 用 `NamedValue` 表示具名值。

`NamedValue` 持有：

- `name`
- `type`

其中 `type` 当前只有两类：

- `UserVariable`
- `Temporary`

它的定位是：

- 在 non-SSA IR 中表达源程序变量和 lowering 临时量
- 为后续 analysis 和 SSA rename 保留最小名字元数据

## 节点集合

当前 non-SSA IR 节点集合是：

- `NumberNode`
- `TextNode`
- `AssignNode`
- `UnaryOpNode`
- `BinOpNode`
- `CallNode`
- `CondJumpNode`
- `JumpNode`
- `ReturnNode`

这里有一个重要约束：

- 当前 IR 是线性语句 IR
- 不是表达式树 IR

例如：

`a = x + y`

在 IR 中应表示为一条 `BinOpNode(result=a, lhs=x, rhs=y)`，而不是在 `AssignNode` 里再嵌一个 rhs 子节点。

## CFG 容器

当前 CFG 容器仍然是：

- `Module`
- `Function`
- `BasicBlock`

这三层是后续所有阶段都应尽量复用的结构层。

### `BasicBlock`

当前 `BasicBlock` 持有：

- 线性 `instructions`
- 一个 `terminal`
- `predecessors`
- `successors`

约束：

- `terminal` 必须在块尾
- 正文里不能混入 terminator 节点
- CFG 边必须双向一致

### `Function`

当前 `Function` 持有：

- 名字
- 类型
- 输入列表
- 输出列表
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

当前 IR 打印器是：

- [src/ir/ir_printer.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir_printer.cpp)

打印风格接近 LLVM IR，但打印对象仍然是 non-SSA IR。

当前打印会输出：

- module 头
- function 头
- block label
- predecessor / successor 注释
- 对齐的源码注释

## 当前 lowering 目标

当前 lowering 的职责很明确：

- AST lower 到 non-SSA IR
- 构建正确 CFG
- 不生成 SSA
- 不生成 `phi`
- 不引入旧 hybrid/value-based 语义

这意味着当前 lowering 应只负责：

- `AST -> Module/Function/BasicBlock/NonSSANode`

而不负责：

- SSA rename
- dominance frontier 插入
- type specialization
- LLVM lowering

## 后续设计边界

后续路线应明确分成三层：

1. `non-SSA IR`
2. `untyped SSA IR`
3. `typed SSA IR`

当前这份文档只讨论第 1 层。

第 2 层和第 3 层不应继续复用当前 `NonSSANode` 语义，而应在 `SSANode` 之下重新定义节点集合。
