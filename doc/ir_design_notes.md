# Baltam_IR IR 设计注意事项

## 目标

本文档整理 Baltam_IR 第一版 IR 的设计原则和基础结构，重点覆盖：

- IR 设计注意事项
- 值类型
- IR 节点
- 常量
- 符号表
- 模块
- 函数
- BasicBlock
- CFG
- 每类 IR 节点的职责

本文档面向第一阶段实现，目标是先支撑：

- `AST -> IR` lowering
- IR 文本打印
- IR 解释执行
- 运行时 profile
- 后续热点 JIT

## 总体原则

建议第一版 IR 采用：

- 非 SSA
- 显式 CFG
- `ValueId` 与 `LocalSlotId` 分离
- 粗粒度静态类型信息
- 强运行时类型标签

原因是这种设计更适合 MATLAB-like 语言的动态语义，也更容易从现有 AST evaluator 平滑迁移。

## 设计注意事项

### 1. 不要直接复用 AST 结构作为 IR

AST 主要服务于解析和语法表达，天然包含大量语法噪声。IR 应只保留执行语义和必要调试信息。

例如：

- 表达式的括号层次对执行通常没有意义
- 语法糖应在 lowering 阶段消解
- `if/while/for` 在 IR 中应变成 block 和 jump，而不是继续保留递归树结构

### 2. 第一版不要急着做 SSA

SSA 对优化很有帮助，但会提高第一版实现成本。当前更务实的做法是：

- 用 `ValueId` 表示临时结果
- 用 `LocalSlotId` 表示局部变量存储
- 用显式 `load/store` 串起数据流

这样更容易先做解释器、profile 和调试。

### 3. 控制流必须显式化

IR 中不能依赖树形嵌套表达控制流。所有分支和循环都应 lower 成：

- `BasicBlock`
- `Jump`
- `Branch`
- `Return`

这是后续 CFG 分析、热点识别和 JIT 的基础。

### 4. 保留源码映射信息

每个 IR 指令和 block 最好都带：

- 源文件
- 行号
- 列号
- 对应 AST 节点引用或 span

这样报错、调试和 profile 展示会容易很多。

### 5. 调用点要区分类型

不要把所有调用统一塞进一个 `call` 节点。建议至少区分：

- `CallBuiltin`
- `CallUserFunction`
- `CallDynamic`

这样才能在解释执行和 JIT 时做不同策略。

### 6. IR 类型信息允许不完整

第一版 lowering 时，不要假设所有值类型都能静态确定。IR 类型信息应允许：

- `Unknown`
- 部分 shape 已知
- 值类别已知但尺寸未知

运行时 profile 会补足这些信息。

## 值类型

建议把类型系统拆成两层：运行时标签和附加类型信息。

### RuntimeTag

运行时标签用于快速分类值。

建议第一版包含：

- `Unknown`
- `Void`
- `DoubleScalar`
- `LogicalScalar`
- `IntScalar`
- `ComplexScalar`
- `String`
- `DenseRealMatrix`
- `DenseComplexMatrix`
- `Range`
- `Cell`
- `Struct`
- `FunctionHandle`
- `Object`

### TypeInfo

`TypeInfo` 描述一个值的粗粒度静态信息或 profile 信息。

建议字段包括：

- `RuntimeTag tag`
- `bool is_scalar`
- `int shape_rank`
- `int rows`
- `int cols`
- `bool has_constant_shape`
- `bool has_constant_value`

第一版不要追求完整类型系统，重点是支撑：

- IR 打印
- profile
- 内建调用快速路径
- 热点专门化

## 常量

建议单独维护常量池，不把常量 payload 直接嵌在每条指令中。

### 常量池设计

`Module` 中保存 `ConstantPool`，每个常量由 `ConstantId` 引用。

好处：

- 常量共享
- 文本 IR 更简洁
- 方便常量折叠
- 方便后续缓存和序列化

### 常量种类

建议第一版支持：

- `NumberConstant`
- `LogicalConstant`
- `IntConstant`
- `StringConstant`
- `ComplexConstant`
- `EmptyMatrixConstant`
- `RangeConstant`

### 常量结构建议

每个常量至少包含：

- `ConstantId`
- `TypeInfo`
- payload

## 符号表

IR 层的符号体系应尽量简化，不应继续依赖“按名字动态查找”作为主要执行机制。

建议拆成两类结构。

### 1. NameTable

供 lowering 使用，负责把源码名字映射到 IR 实体。

映射对象包括：

- 局部变量名 -> `LocalSlotId`
- 参数名 -> `LocalSlotId`
- 返回值名 -> `LocalSlotId`
- 函数名 -> `FunctionId` 或符号引用

### 2. Symbol

供模块级组织和调试使用。

建议至少包含：

- `LocalSlotSymbol`
- `FunctionSymbol`
- `BuiltinSymbol`
- `GlobalSymbol`

IR 执行时应优先按：

- `LocalSlotId`
- `FunctionId`
- `BuiltinId`

访问，而不是频繁做字符串查找。

## 模块

`Module` 是 IR 的编译、缓存和调试单位。建议一个 `.m` 文件 lower 成一个 `Module`。

### Module 建议包含

- 模块名
- 源文件路径
- 常量池
- 函数列表
- 顶层脚本函数
- builtin 引用表
- 调试信息
- 全局符号信息

### 特别建议

即使输入是脚本，也建议把脚本体表示成一个特殊函数，例如 `__script_main__`。这样顶层代码和普通函数能复用同一套执行模型。

## 函数

`Function` 是 IR 的核心执行单元。

### Function 建议包含

- `FunctionId`
- 函数名
- 输入参数槽位列表
- 输出参数槽位列表
- 局部槽位表
- 临时值计数或分配信息
- `BasicBlock` 列表
- 入口块 id
- profile 信息
- 调试信息

### 槽位建议

局部变量、参数和返回值统一落在 `LocalSlotId` 空间里。这样解释器模型更简单。

## BasicBlock

`BasicBlock` 是显式控制流的基本单位。

### BasicBlock 建议包含

- `BasicBlockId`
- 名字
- 普通指令列表
- 终结指令
- 前驱列表
- 后继列表
- 调试信息

### BasicBlock 规则

- block 中间不能出现控制流终结
- 每个 block 只能有一个 terminator
- terminator 只能是 `Jump`、`Branch`、`Return`

## CFG

CFG 应显式保存在函数对象中，而不是临时推导。

### CFG 建议包含

- block 集合
- 入口块
- `preds`
- `succs`
- 可选的回边信息
- 可选的 loop header 信息

显式 CFG 的好处：

- 便于做死块删除
- 便于做 profile
- 便于识别热点循环
- 便于未来做 dominator 和 SSA

## ValueId、LocalSlotId 与 ConstantId

建议明确区分三种引用。

### ValueId

表示指令产生的临时结果。

特点：

- 生命周期通常较短
- 主要在 block 内或跨 block 临时传递
- 不直接代表源码变量

### LocalSlotId

表示局部变量、参数或返回值的存储位置。

特点：

- 与源码级名字更接近
- 是解释器 frame 的存储单元
- 由 `LoadLocal` 和 `StoreLocal` 读写

### ConstantId

表示常量池中的常量。

### 例子

源码：

```matlab
a = 1 + 2;
```

建议 lower 成：

```text
v0 = Const #0
v1 = Const #1
v2 = BinaryOp add, v0, v1
StoreLocal a_slot, v2
```

而不是把变量名直接塞到运算指令里。

## IR 节点公共字段

建议所有 IR 指令都继承统一的基础信息。

至少包含：

- `InstrId`
- `Opcode`
- 结果值列表
- 操作数列表
- `TypeInfo`
- `SourceLocation`
- `EffectFlags`
- `ProfileSiteId`

### EffectFlags

建议第一版至少区分：

- `NoSideEffect`
- `MayThrow`
- `MayReadMemory`
- `MayWriteMemory`
- `MayCall`

这对后续优化和 JIT 非常重要。

## IR 节点分类

下面列出建议的第一版 IR 节点。

### 1. Const

职责：

- 从常量池取出一个常量值

字段：

- `dst: ValueId`
- `constant_id: ConstantId`

说明：

- 常量值统一通过常量池管理

### 2. LoadLocal

职责：

- 从局部槽位读取值

字段：

- `dst: ValueId`
- `slot: LocalSlotId`

### 3. StoreLocal

职责：

- 将一个值写入局部槽位

字段：

- `slot: LocalSlotId`
- `value: ValueId`

### 4. Move

职责：

- 显式复制一个值

字段：

- `dst: ValueId`
- `src: ValueId`

说明：

- 第一版中可选
- 有助于简化 lowering 和后续寄存器化

### 5. UnaryOp

职责：

- 表示一元运算

字段：

- `dst: ValueId`
- `op`
- `arg: ValueId`

典型运算：

- 取负
- 逻辑非

### 6. BinaryOp

职责：

- 表示二元算术运算

字段：

- `dst: ValueId`
- `op`
- `lhs: ValueId`
- `rhs: ValueId`

典型运算：

- `add`
- `sub`
- `mul`
- `div`

### 7. CompareOp

职责：

- 表示比较运算

字段：

- `dst: ValueId`
- `op`
- `lhs: ValueId`
- `rhs: ValueId`

典型运算：

- `gt`
- `lt`
- `ge`
- `le`
- `eq`
- `ne`

### 8. CallBuiltin

职责：

- 调用内建函数

字段：

- `dsts`
- `builtin_id`
- `args`

说明：

- 内建调用是最值得优化的调用类型之一
- 第一版就应单独建节点

### 9. CallUserFunction

职责：

- 调用已解析到的用户函数

字段：

- `dsts`
- `function_id`
- `args`

### 10. CallDynamic

职责：

- 调用运行时才能确定目标的函数或句柄

字段：

- `dsts`
- `callee_value`
- `args`

说明：

- 用于函数句柄、复杂动态调用等场景
- 第一版可以先保守实现

### 11. Cast

职责：

- 表示显式类型转换或特化检查后的值投影

字段：

- `dst`
- `src`
- `target_type`

说明：

- 第一版可弱化
- 但建议预留节点，为 JIT 和 guard 专门化做准备

### 12. PhiLikeCopy

职责：

- 在非 SSA IR 中处理合流点的数据并槽

字段：

- 一组 copy 映射

说明：

- 第一版不一定必须有
- 如果分支合流处理复杂，可用它简化 lowering

### 13. Jump

职责：

- 无条件跳转到目标 block

字段：

- `target_bb`

### 14. Branch

职责：

- 条件分支

字段：

- `cond`
- `true_bb`
- `false_bb`

说明：

- `cond` 的求值语义要与 MATLAB-like 语言一致

### 15. Return

职责：

- 返回当前函数

字段：

- 返回值列表

### 16. DeoptGuard

职责：

- 检查专门化假设是否成立

字段：

- 被检查的值
- 假设信息
- 失败时回退位置

说明：

- 第一版解释器可以不真正执行该节点
- 但建议提前在 IR 模型中预留

## MATLAB-like 语言的特殊注意事项

### 1. 标量与矩阵要统一值模型

第一版 IR 不要为标量和矩阵设计完全不同的节点系统。应统一在值模型中处理，并通过：

- `RuntimeTag`
- `TypeInfo`
- profile

去区分热点类型。

### 2. builtin 调用必须保留独立节点

像 `sin`、`cos`、`exp`、`plus` 这类调用是优化重点，不建议过早泛化成统一动态调用。

### 3. 名字解析尽量在 lowering 后固定

如果 IR 执行时还大量依赖字符串查找，后续性能和 JIT 都会受影响。应尽量在 lowering 阶段把稳定引用转换为：

- `LocalSlotId`
- `FunctionId`
- `BuiltinId`

### 4. 高动态语义应先允许 fallback

例如：

- `eval`
- 动态字段访问
- 复杂 `cell/struct`
- 高度动态索引
- 不稳定函数句柄调用

第一版不必强行纳入核心 IR 快路径，可以保守处理或回退。

## 推荐的第一版最小落地集合

如果目标是尽快跑通 `test/simple_demo.m`，建议第一版至少实现：

- 常量池
- `Const`
- `LoadLocal`
- `StoreLocal`
- `BinaryOp`
- `CompareOp`
- `CallBuiltin`
- `Branch`
- `Jump`
- `Return`
- `Module`
- `Function`
- `BasicBlock`
- `CFG`

这套最小集合已经足够支撑：

- `1 + 2`
- `sin(a)`
- `if b > 0`
- 局部变量读写

## 建议的后续顺序

推荐按下面顺序实现：

1. 定义 IR 数据结构
2. 实现 IR 文本打印
3. 先支持 `simple_demo.m` 的 lowering
4. 实现最小 IR 解释器
5. 为 slot 和 callsite 增加 profile
6. 再进入热点 JIT 设计

## 总结

第一版 IR 最重要的不是“理论上最优”，而是：

- 结构清晰
- 能从现有 AST evaluator 平滑迁移
- 能支撑解释执行
- 能承接 profile 和 JIT

因此，当前最合理的设计方向是：

- 非 SSA
- 显式 CFG
- slot/value 分离
- 常量池
- 模块/函数/block 分层
- 调用点分类
- 保留调试与回退能力
