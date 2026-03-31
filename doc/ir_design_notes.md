# Baltam_IR IR 设计注意事项

## 目标

本文档描述当前仓库已经落地的 IR 设计，以及后续继续演进时应保持的约束。

它不再讨论早期那套 `LocalSlotId + LoadLocal/StoreLocal` 的旧方案，而以当前代码中的实际 IR 为准。

## 当前设计总览

当前 IR 的核心特征是：

- 显式 CFG
- 显式 `BasicBlock`
- 显式 terminator
- value-based 结果模型：`ValueId` / `ValueRef` / `InstValue`
- 保留名字环境语义：`BindingInstruction` / `AssignInstruction`
- 已支持 `PhiInstruction`

这意味着当前 IR 已经不是纯名字 IR，但也还不是完整 SSA，而是“值结果 + 名字环境并存”的 hybrid 设计。

## 核心对象

### 1. 值结果

当前值结果模型由三部分组成：

- `ValueId`
- `ValueRef`
- `InstValue`

约束是：

- 一个 `ValueId` 只对应一次定义
- 一条指令可以定义 0 个、1 个或多个结果
- 结果值可以带 `debug_name` 和 `SourceLocation`

这套模型已经是后续做 def-use、verifier 和 SSA 构建的基础。

### 2. 指令

当前 IR 已有的主要指令类型包括：

- `Text`
- `Binding`
- `Number`
- `UnaryOp`
- `BinOp`
- `Phi`
- `Asgn`
- `Call`
- `CondJump`
- `Jump`
- `Return`

其中：

- 计算类指令优先通过 `ValueRef` 传递依赖
- `PhiInstruction` 用于 CFG 合流点
- `CallInstruction` 已支持多结果定义
- `BindingInstruction` / `AssignInstruction` 仍然保留名字环境语义

### 3. BasicBlock

`BasicBlock` 是当前 IR 的显式控制流单元，包含：

- 普通指令序列
- 一个可选的 terminator
- 前驱列表
- 后继列表

应继续保持的规则是：

- terminator 只能出现在块尾
- 每个块至多一个 terminator
- `phi` 必须位于块首并在普通指令之前求值

### 4. Function / Module

`Function` 负责拥有：

- 输入参数名
- 输入参数对应的值定义
- 输出参数名
- `BasicBlock`
- 指令存储
- `ValueId` 分配

`Module` 负责组织：

- 源文件路径
- 顶层函数集合
- 入口函数

当前这层组织已经足够支撑：

- lowering
- IR 打印
- 解释执行
- 后续 analysis / optimizer pass

## 当前设计原则

### 1. 计算数据流优先走 `ValueRef`

纯计算语义应尽量通过结果值和 `ValueRef` 表达，而不是继续回到名字字符串。

这包括：

- 字面量
- 一元运算
- 二元运算
- 调用输入
- 条件跳转条件
- 返回值

### 2. 名字环境语义只保留在必要位置

当前 `BindingInstruction` 和 `AssignInstruction` 仍然是有意义的，因为项目还需要承接 MATLAB-like 的名字环境语义。

但它们不应继续膨胀成主要数据流载体。

长期方向应是：

- 纯局部计算更多只依赖 `ValueRef`
- 名字环境只保留在真正可观察的读写边界

### 3. 多结果调用是一等公民

MATLAB-like 语言天然有多返回值调用，因此当前 IR 应继续把“一个调用定义多个结果值”当作正常情况，而不是异常情况。

后续优化和 SSA 构建都应直接支持这一点。

### 4. 调试信息不能丢

当前 IR 已保留：

- `SourceLocation`
- `debug_name`

后续无论做 verifier、分析还是 SSA rename，都应继续把这两类信息当作一等元数据保留。

### 5. 结构化 lowering 可以继续存在，但不应成为长期唯一 SSA 来源

当前 lowering 已经会在 `if/while/for/switch` 的结构化合流点直接插入 `phi`。

这在当前阶段是合理的，但长期不应把所有 SSA 逻辑继续堆在 lowering 中。

成熟路线应逐步演进到：

- lowering 负责生成正确 CFG
- 中端独立 pass 负责 `BuildPrunedSSA`

## 已经过时的旧设想

下面这些内容已经不再适合作为当前 IR 的主设计：

- `LocalSlotId` 作为主局部变量模型
- `LoadLocal` / `StoreLocal` 作为主数据流节点
- 独立常量池作为当前 IR 必选前提
- 按 `CallBuiltin` / `CallUserFunction` / `CallDynamic` 拆分调用节点

这些思路并非永远不能做，但它们都不是当前代码实际采用的 IR 结构。

## 后续演进方向

如果继续沿当前 IR 演进，下一批更值得做的基础设施是：

1. `Verifier`
2. `CFGAnalysis`
3. `DominatorTree`
4. `DominanceFrontier`
5. `DefUse`
6. `BuildPrunedSSA`

同时继续推进：

- 收缩 `AssignInstruction` / `BindingInstruction` 的职责
- 让更多纯局部路径只依赖值结果
- 在优化器中把 SSA 构建从 lowering 中逐步解耦

## 总结

当前 Baltam_IR 的设计重点已经不是“先发明一套全新的非 SSA slot IR”，而是：

- 在现有 value-based IR 上继续补分析层
- 保持名字环境语义与局部值路径分层
- 为独立的 SSA pass 和后续优化器打基础
