# Matlab IR 草案

本文记录 high-level IR 的设计目标和分层原则。当前实现的字段级 schema 以
[ir_schema.md](./ir_schema.md) 为准；本文不再重复具体 C++ 结构，避免和源码事实漂移。

## 目标

当前默认的总体流水线是：

```text
AST -> high-level IR -> IR interpreter/profile -> typed SSA -> LLVM IR
```

当前架构明确不引入独立的中间执行 IR。解释器直接消费本 high-level IR，避免维护第二套指令
schema、verifier、调试映射和动态语义实现。

其中 high-level IR 的角色是：

- 保留 Matlab / M 语言的高层动态语义边界
- 为解释器和 JIT 提供直接输入
- 作为后续热点编译前的语义规范化层
- 给 verifier、printer、CFG 可视化和 smoke test 提供统一对象模型

## 总体取舍

当前主方向是：

- `IR` 承担 Matlab 动态语义的主建模责任
- high-level IR interpreter 承担稳定执行、profile 和优化代码回退
- `typed SSA` 只在热点 region 或热点函数上按需构造

这样分层的核心原因不是“SSA 不好”，而是 Matlab 的完整动态语义不适合作为全局常驻 SSA 的
主干表示。尤其是：

- 脚本 `ScriptVar` slot 的运行时绑定和失效
- 动态 env 访问
- 动态名字解析
- 动态函数分派
- `eval`
- `clear`
- `cd`
- `addpath`
- mex / 外部扩展交互

如果把这些行为强行压进一个长期保留的 SSA 主链路，通常会演化成大量 barrier、失效点和
保守副作用建模，最后反过来削弱 SSA 本来最有价值的优化能力。

因此当前职责分工是：

- `IR`：语义层 IR，显式表达动态环境、名字、调用和控制流边界。
- `IR interpreter`：直接执行 high-level IR，负责完整语义、profile，并作为 deopt 回退目标。
- `typed SSA`：热点优化 IR，只服务于数值热点和相对稳定的 region。

更适合进入 typed SSA 的部分通常是：

- 只涉及 frame 和有限 heap 的数值代码
- loop body 内稳定的 slot 读写
- 已知稳定 builtin 路径上的算术和比较
- 已经通过分析收敛的 `call` / `value_apply`

更适合作为 SSA region 边界的部分通常是：

- 脚本 `ScriptVar` 绑定失效点
- 动态 env 读写
- `eval` / `evalin` / `assignin`
- `clear`
- path 修改
- mex 交互
- 强动态调用分派

## 非目标

这层 IR 不承担以下职责：

- 不要求全局 SSA 形式
- 不作为主要优化 IR
- 不直接作为解释执行 IR
- 不直接携带 JIT guard、deopt state map、OSR metadata
- 不一次性覆盖全部 Matlab 高级特性细节

## 当前语义范围

当前 high-level IR 已经覆盖：

- `.m` 文件输入
- 脚本、函数、文件内 local function
- module 级匿名函数体
- slot / value / basic block / instruction 基础对象模型
- 脚本 `ScriptVar` slot、函数 frame slot 和动态 env 边界
- `apply`、`value_apply`、`call`
- 具名函数句柄和匿名函数句柄
- 多返回值和 `~` 占位输出位
- 常见控制流：`if`、`switch`、`for`、`while`、`break`、`continue`、`return`

仍属于后续工作的范围：

- REPL / command lowering
- 嵌套函数
- `try/catch`
- `global` / `persistent`
- 完整索引、成员访问和完整 Matlab 函数优先级系统

## 核心原则

### 1. 语义优先

凡是会影响 Matlab 动态语义的行为，都应在 IR 中显式出现，而不是隐藏在普通变量读写里。

脚本中静态出现的变量也应进入 slot 表，tag 为 `ScriptVar`。它的 IR 读写仍打印为
`load` / `store`，但执行层需要根据运行时绑定状态决定直接访问已绑定存储，还是在绑定失效后
退回动态 lookup。函数中的静态局部变量则使用 `Local/Arg/Ret` 等普通 frame slot；对用户
可见的名字，`SlotId` 只表达静态身份，`clear` / `eval` 等机制仍可能让当前 activation 中的
live binding 失效，后续 slot 读写需要由执行层或前置分析确认 binding 仍然有效。

### 2. 静态事实向下传递，动态边界显式保留

IR 的职责不是把所有语义都提前静态化，而是在当前阶段做清楚两件事：

- 已经确定的静态语义，应显式传递给后续 interpreter、profile、JIT 和分析 pass。
- 仍然依赖运行时环境的动态语义，应保留为可见的 IR 边界，而不是通过普通变量读写或普通调用
  暗中表达。

例如：

- 函数入参、出参和普通局部变量的 slot layout 应向下传递；其 use 只有在当前 binding 仍 live
  时才表达为 `LoadSlotInst` / `StoreSlotInst`。
- 后续接入的 `global` 应通过 `Slot::Global` 指向 `GlobalRegistry::values[name]` 显式读写；
  `persistent` 可以使用静态 `SlotId` 定位，但二者都应有不同于普通 frame slot 的显式读写语义。
- 脚本名字访问、`eval`、`assignin`、路径变化和仍未消歧的 `A(...)` 应保持 workspace、Env
  effect 或动态应用边界。
- 已经解析稳定的调用可以表达为 `call`；仍可能受 workspace 遮蔽或运行时分派影响的应用应保留
  为 `apply` 或 `value_apply`。

这条原则的目标是让 IR 同时服务两类后续需求：IR interpreter 可以按完整动态语义执行；
优化层可以只提取已经静态稳定或有 guard 保护的区域。

### 3. Non-SSA，但允许局部 ValueId

IR 不是 SSA IR，但允许用 `ValueId` 表达指令结果，方便表达式级数据流。真正的可变程序状态
仍然通过 slot 或动态 env 表达。

因此：

- block 内可以有短生命周期值
- block 间的可变状态必须落入 slot，或在动态语言机制下落入 env
- 多结果指令可以产生多个 `ValueId`
- `~` 占位输出位保留结果位次，但没有真实 `ValueId`

### 4. 静态结构与运行时对象分层

`IRModule`、`MFileUnit`、`CodeUnit`、`AnonymousFunctionUnit` 是编译期组织结构。运行时函数
句柄、closure code object、capture environment 等执行对象不应简单强持有整个编译期
MFile/module。

这条原则对匿名函数尤其重要：IR 中可以用 module 级匿名函数表组织 body，但 runtime 句柄
应持有可执行 closure code 和 captures。

### 5. 便于直接解释执行

IR 应容易由解释器直接执行：

- slot 可以映射到 frame layout
- block 和 terminator 可以直接驱动解释器控制流
- dynamic env / dynamic call 语义保持显式
- 不在 lowering 阶段偷偷静态化脚本 slot 的运行时绑定或函数路径

### 6. 便于后续 region 提取

虽然 IR 不是主优化 IR，但它应清楚表达哪些区域适合进入 typed SSA，哪些区域必须留在解释
执行语义里。

这要求 effect、动态 env、call dispatch、slot 读写和 CFG 边界都保持可分析。

## 相关文档

- [IR Schema](./ir_schema.md)
- [IR Lowering 设计](./ir_lowering_design.md)
- [IR Builder 设计](./ir_builder_design.md)
- [IR Verifier 设计](./ir_verifier_design.md)
- [Execution Strategy](./execution_strategy.md)
- [M 变量模型设计](./variable_model_design.md)
- [M 工作区设计](./workspace_design.md)
- [InterpreterContext、CodeObject 与 Frame 设计](./runtime_execution_objects_design.md)
- [Global / Persistent IR 节点设计](./global_persistent_ir_design.md)
- [Deopt 与优化运行时参考资料](./deopt_runtime_references.md)
