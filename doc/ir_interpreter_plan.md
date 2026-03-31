# Baltam_IR 解释执行思路

## 目标

本文档描述当前仓库已经落地的 IR 解释器模型，以及接下来更值得推进的方向。

它不再讨论早期基于 `NameInstruction`、`out_args` 或 `ForRangeInstruction` 的旧设计。

## 当前状态

当前项目已经具备完整的：

- `AST -> IR lowering`
- IR 文本打印
- IR 解释执行

解释器入口是 `execute_function(Function&, args, caller)`，运行时值统一为：

```cpp
using Value = std::shared_ptr<ba_obj>;
```

## 运行时模型

### 1. Frame

当前 `Frame` 同时维护两套状态：

- 名字环境：`SymbolTable`
- 值结果表：`ValueTable`

也就是说，当前解释器不是纯名字模型，也不是纯 SSA 执行器，而是 hybrid 模型：

- `AssignInstruction` 仍会写名字环境
- 计算结果会写入 `ValueId`
- 后续指令优先按 `ValueRef` 取值

### 2. 输入与输出

当前执行模型里：

- 函数输入参数会先写入对应 `input_ref()` 的值槽
- 输出参数名会在建帧时预声明
- 显式 `ReturnInstruction` 优先返回自身携带的 `ValueRef` 列表
- 如果函数自然结束，则仍可按输出名字收集结果

这与当前 IR“值结果 + 名字环境并存”的设计保持一致。

## 执行循环

当前 block 调度的关键语义是：

1. 从 `entry_block()` 开始
2. 进入块时先执行块首 `phi`
3. 再执行普通指令
4. 最后执行 terminator，决定下一个块

这里最重要的约束是：

- `phi` 只能出现在块首
- `phi` 不能按普通指令执行
- `phi` 的 incoming 由前驱块决定

这套语义已经是完整 SSA block-entry 规则的雏形。

## 求值与物化

### 1. 普通求值

当前解释器会对下面这些节点做表达式求值：

- `BindingInstruction`
- `NumberInstruction`
- `UnaryOpInstruction`
- `BinOpInstruction`
- `CallInstruction`
- `PhiInstruction`

其中：

- `PhiInstruction` 不能临时现算，必须在 block 入口预先求值
- 其余产值节点可按需物化

### 2. `ValueRef` 读取

当前取值逻辑的关键点是：

- 优先从 `Frame::ValueTable` 读取
- 若值槽中尚未写入，则按 `ValueId -> owner instruction` 回溯物化

这意味着当前解释器已经不再只是“按顺序执行并把结果全丢进名字表”，而是具备了基本的 value-based 物化能力。

## 运行时桥接

当前解释器依赖运行时桥接层完成：

- 一元运算
- 二元运算
- 条件判断
- 内建函数调用
- 内部函数调用

解释器本身的职责应继续保持克制：

- 不重做值系统
- 不重做 builtin 实现
- 不重做对象系统
- 只负责调度 IR 与 runtime 之间的连接

## 循环语义

当前 `for` 并不是通过专门循环节点执行，而是 lower 成显式 CFG，并通过运行时协议配合：

- `foreach_init`
- `foreach_iterate`

这条路径已经比早期“专门循环指令”更接近长期可维护的方案。

因此后续应继续坚持：

- `for i = expr` 统一建模
- `node_colon` 作为普通表达式 lower
- 循环语义由 CFG + runtime helper 共同表达

## 当前解释器的主要约束

### 1. 名字环境仍然活跃

当前很多可观察语义仍然依赖名字环境，因此不能把解释器直接当成“纯 SSA 执行器”。

### 2. 还没有 verifier 保底

目前解释器能执行当前 IR，但中间还缺：

- CFG verifier
- phi 完整性检查
- use-def 检查

如果后续引入优化和 SSA pass，这一层必须先补。

### 3. 还没有 optimizer 集成

当前执行链仍然主要是：

`lower -> print -> execute`

后续更合理的形态应是：

`lower -> verify -> optimize(optional) -> execute`

## 接下来的重点

如果只看解释器这条线，后续更值得推进的是：

1. 扩大 lowering 可覆盖的语法子集
2. 扩大解释器可正确执行的 IR 子集
3. 补 `Verifier`
4. 在执行链中加入 `PassManager` / `optimize_module()`
5. 为 profile 和优化预留更清晰的插桩点

不建议当前优先做的事情包括：

- 把解释器整套重写成纯 SSA 执行器
- 为解释器单独再发明一套新 IR
- 在没有 verifier 的情况下叠加复杂优化

## 总结

当前 Baltam_IR 解释器已经不是“最小 demo 设想”，而是一条真实可运行的主执行链。

接下来最重要的不是再换解释执行模型，而是：

- 继续做厚当前 `IR + Interpreter`
- 给它补 verifier 和 optimizer 入口
- 让后续 SSA 和优化器建立在这条已跑通的主链上
