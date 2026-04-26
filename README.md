# Baltam IR

`Baltam_IR` 是一个面向 `M` 语言 / Matlab 风格语言的中间表示实验仓库。这个仓库想做的事情，不只是把 `AST` 打印成另一种文本，而是逐步建立一套能承接高层动态语义、又能继续向 bytecode 和后续优化阶段 lowering 的 `IR` 基础设施。

当前设想的主流水线是：

`AST -> IR -> bytecode -> interpreter/profile -> typed SSA -> LLVM IR`

其中，当前仓库主要关注 `IR` 这一层本身，以及围绕它的构建、lowering 和打印能力。

## 仓库想做什么

这个仓库希望逐步搭出一条面向动态 `M` 语言的编译 / 执行前端链路，核心目标包括：

- 设计一套适合 `M` 语言语义的 high-level IR
- 明确 `IR`、bytecode、runtime、typed SSA` 之间的职责边界
- 把脚本、函数、名字绑定、调用分派等高层语义先稳定地落到 `IR`
- 为后续解释执行、profile、热点优化和 `LLVM IR` lowering 提供基础

这个方向的重点不是“尽快做一个 SSA IR”，而是先把动态语言最难处理的边界表达清楚，例如：

- workspace 访问
- 动态名字解析
- 调用分派
- `eval` / `clear` / `cd` / `addpath`
- builtin / mex / 外部扩展接口

## 目前已经做到的

当前仓库已经完成了 `IR` 的一版最小闭环实现，主要包括：

- `IR` 基础对象模型
- `IRBuilder`
- `IRLowerer`
- `IRPrinter`

目前已经支持的范围包括：

- `.m` 文件到 `IR` 的 parse + lowering 闭环
- `script` 和 `function` 两类代码单元
- 简单赋值语句
- 数值字面量
- 名字读取
- 名字形式的圆括号应用
- 带输出参数的圆括号应用语句
- `if / else`
- 显式 `return` 与隐式 `return`

在现有实现里：

- 脚本变量访问会 lower 成 `load_workspace` / `store_workspace`
- 函数变量访问会 lower 成 `load_slot` / `store_slot`
- 函数中的名字调用会根据名字绑定情况，在 `Apply` 和 `Call` 之间分派

当前仓库已经有基础 smoke test：

- [test0.m](/home/zj/Desktop/Baltam_IR/test/m/test0/test0.m)
- [test0_1.m](/home/zj/Desktop/Baltam_IR/test/test0_1.m)
- [test0_smoke.cpp](/home/zj/Desktop/Baltam_IR/test/smoke_test/test0_smoke.cpp)
- [test0_1_smoke.cpp](/home/zj/Desktop/Baltam_IR/test/smoke_test/test0_1_smoke.cpp)

## 后续要做的

接下来更重要的工作主要在这几个方向：

- 支持脚本 / 函数中的 `local` 函数
- 继续补齐 `IR` 的表达能力，例如更复杂的调用、成员访问和索引语义
- 逐步完善名字解析和调用分派规则
- 梳理 `eval`、`clear`、路径变化等对环境稳定性的影响
- 为 bytecode lowering 和 runtime 设计补足语义接口
- 为后续 profile、guard、热点优化和 typed SSA 做准备

从设计角度看，当前仓库仍然处在“先把语义边界和对象模型打稳”的阶段，而不是“全面铺开优化”的阶段。

## 相关文档

如果需要看更细的设计说明，可以从这些文档开始：

- [IR 草案](./doc/ir_draft.md)
- [IR Schema](./doc/ir_schema.md)
- [IR Builder Design](./doc/ir_builder_design.md)
- [IR Lowering Design](./doc/ir_lowering_design.md)
- [计划中与思考中的问题](./doc/planning_notes.md)
- [Execution Strategy](./doc/execution_strategy.md)
- [更新日志](./doc/update_notes.md)
