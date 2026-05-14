# Matlab IR 草案

本文记录 high-level IR 的设计目标和分层原则。当前实现的字段级 schema 以
[ir_schema.md](./ir_schema.md) 为准；本文不再重复具体 C++ 结构，避免和源码事实漂移。

## 目标

当前默认的总体流水线是：

```text
AST -> IR -> bytecode -> interpreter/profile -> typed SSA -> LLVM IR
```

其中 high-level IR 的角色是：

- 保留 Matlab / M 语言的高层动态语义边界
- 为 bytecode lowering 提供直接输入
- 作为后续热点编译前的语义规范化层
- 给 verifier、printer、CFG 可视化和 smoke test 提供统一对象模型

## 总体取舍

当前主方向是：

- `IR` 承担 Matlab 动态语义的主建模责任
- `bytecode` 承担稳定执行、profile 和解释器回退
- `typed SSA` 只在热点 region 或热点函数上按需构造

这样分层的核心原因不是“SSA 不好”，而是 Matlab 的完整动态语义不适合作为全局常驻 SSA 的
主干表示。尤其是：

- workspace 访问
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
- `bytecode`：执行层 IR，负责解释执行、profile 和作为 deopt 回退目标。
- `typed SSA`：热点优化 IR，只服务于数值热点和相对稳定的 region。

更适合进入 typed SSA 的部分通常是：

- 只涉及 frame 和有限 heap 的数值代码
- loop body 内稳定的 slot 读写
- 已知稳定 builtin 路径上的算术和比较
- 已经通过分析收敛的 `call` / `value_apply`

更适合作为 SSA region 边界的部分通常是：

- workspace 读写
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
- workspace 读写和 frame slot 读写
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

脚本中的名字读写应先保留为 workspace 访问；函数中的静态局部变量才使用 slot 访问。后续
pass 可以在证明名字稳定后再收敛为更低成本的形式。

### 2. Non-SSA，但允许局部 ValueId

IR 不是 SSA IR，但允许用 `ValueId` 表达指令结果，方便表达式级数据流。真正的可变程序状态
仍然通过 slot 或 workspace 表达。

因此：

- block 内可以有短生命周期值
- block 间的可变状态必须落入 slot / workspace
- 多结果指令可以产生多个 `ValueId`
- `~` 占位输出位保留结果位次，但没有真实 `ValueId`

### 3. 静态结构与运行时对象分层

`IRModule`、`MFileUnit`、`CodeUnit`、`AnonymousFunctionUnit` 是编译期组织结构。运行时函数
句柄、closure code object、capture environment 等执行对象不应简单强持有整个编译期
MFile/module。

这条原则对匿名函数尤其重要：IR 中可以用 module 级匿名函数表组织 body，但 runtime 句柄
应持有可执行 closure code 和 captures。

### 4. 便于 bytecode lowering

IR 应容易 lowering 到 bytecode：

- slot 可以映射到 frame layout
- block 可以线性化为 bytecode 基本块和跳转
- dynamic env / dynamic call 语义保持显式
- 不在 lowering 阶段偷偷静态化 workspace 名字或函数路径

### 5. 便于后续 region 提取

虽然 IR 不是主优化 IR，但它应清楚表达哪些区域适合进入 typed SSA，哪些区域必须留在解释
执行语义里。

这要求 effect、workspace、call dispatch、slot 读写和 CFG 边界都保持可分析。

## 相关文档

- [IR Schema](./ir_schema.md)
- [IR Lowering 设计](./ir_lowering_design.md)
- [IR Builder 设计](./ir_builder_design.md)
- [IR Verifier 设计](./ir_verifier_design.md)
- [Execution Strategy](./execution_strategy.md)
