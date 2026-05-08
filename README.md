# Baltam IR

`Baltam_IR` 是一个面向 `M` 语言 / Matlab 风格语言的中间表示实验仓库。这个仓库想做的事情，不只是把 `AST` 打印成另一种文本，而是逐步建立一套能承接高层动态语义、又能继续向 bytecode 和后续优化阶段 lowering 的 `IR` 基础设施。

当前设想的主流水线是：

`AST -> IR -> bytecode -> interpreter/profile -> typed SSA -> LLVM IR`

其中，当前仓库主要关注 `IR` 这一层本身，以及围绕它的构建、lowering 和打印能力。

如果只想快速建立当前文档结构的全局印象，可以先按这个顺序看：

1. [README](./README.md)
2. [IR Lowering Design](./doc/ir_lowering_design.md)
3. [循环 Lowering 设计](./doc/loop_lowering_design.md)
4. [CFG DOT 输出设计](./doc/cfg_dot_design.md)
5. [计划中与思考中的问题](./doc/planning_notes.md)
6. [后续需要学习与确认的问题](./doc/learning_notes.md)

## 仓库想做什么

这个仓库希望逐步搭出一条面向动态 `M` 语言的编译 / 执行前端链路，核心目标包括：

- 设计一套适合 `M` 语言语义的 high-level IR
- 明确 `IR`、bytecode、runtime、`typed SSA` 之间的职责边界
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
- `IRVerifier`
- `ValueId` 类型事实表示
- `ir_print` 文本 IR 打印工具
- `ir_cfg_dot` 控制流图导出工具

目前已经支持的范围包括：

- `.m` 文件到 `IR` 的 parse + lowering 闭环
- `script` 和 `function` 两类代码单元
- 简单赋值语句
- 数值字面量
- 名字读取
- 一元运算与二元运算
- 名字形式的圆括号应用
- 带输出参数的圆括号应用语句
- 文件内 `local` 函数的最小支持
- `if / else`
- `for` / `while` 循环
- 循环内 `break / continue`
- 嵌套 `for`、嵌套 `while`、以及 `for / while` 混合嵌套
- `switch / case / otherwise`
- 显式 `return` 与隐式 `return`

在现有实现里：

- 脚本变量访问会 lower 成 `LoadWorkspaceInst` / `StoreWorkspaceInst`，文本 IR 打印为
  `load_env` / `store_env`
- 函数变量访问会 lower 成 `load_slot` / `store_slot`
- 函数中的名字调用会根据名字绑定情况，在 `Apply` 和 `Call` 之间分派
- 对于文件内 `local` 函数：
  - 脚本主体中的名字调用当前仍保留为 `Apply`
  - 函数主体中的名字调用若命中 `local` 函数，可直接分派为 `Call`
  - 函数主体中的一元 / 二元运算若命中同名 local 函数，也可直接分派为
    `dispatch_type = MFunction`，打印为 `call mfunc`
  - 若这些名字已经被局部变量遮蔽，则回退为普通运算或 `Apply`

当前仓库已经有基础 smoke test：

- [test0.m](/home/zj/Desktop/Baltam_IR/test/m/test0/test0.m)
- [test0_1.m](/home/zj/Desktop/Baltam_IR/test/m/test0/test0_1.m)
- [test1.m](/home/zj/Desktop/Baltam_IR/test/m/test1/test1.m)
- [test1_1.m](/home/zj/Desktop/Baltam_IR/test/m/test1/test1_1.m)
- [test2.m](/home/zj/Desktop/Baltam_IR/test/m/test2/test2.m) 到
  [test2_3.m](/home/zj/Desktop/Baltam_IR/test/m/test2/test2_3.m)
- [test3.m](/home/zj/Desktop/Baltam_IR/test/m/test3/test3.m) 到
  [test3_3.m](/home/zj/Desktop/Baltam_IR/test/m/test3/test3_3.m)
- [test4.m](/home/zj/Desktop/Baltam_IR/test/m/test4/test4.m) 到
  [test4_3.m](/home/zj/Desktop/Baltam_IR/test/m/test4/test4_3.m)
- 语法闭环测试位于 [test/smoke_test/syntax](/home/zj/Desktop/Baltam_IR/test/smoke_test/syntax)
- 功能 smoke test 位于 [test/smoke_test/feature](/home/zj/Desktop/Baltam_IR/test/smoke_test/feature)

## 常用工具

构建后可以直接把 `.m` 文件打印为文本 IR：

```bash
./build/ir_print test/m/test4/test4.m -o /tmp/test4.ir
```

也可以生成 CFG DOT 或图片：

```bash
./build/ir_cfg_dot test/m/test4/test4.m -o /tmp/test4.dot --svg /tmp/test4.svg
```

如果本机 runtime / builtin 动态库加载失败，按 smoke test 使用的环境补充：

```bash
env HOME=/tmp XDG_CACHE_HOME=/tmp/.cache BALTAM_FRONTEND=console \
  LD_LIBRARY_PATH=/opt/Baltamatica/lib \
  ./build/ir_print test/m/test4/test4.m -o /tmp/test4.ir
```

## 后续要做的

接下来更重要的工作主要在这几个方向：

- 继续补齐 `IR` 的表达能力，例如更复杂的调用、成员访问和索引语义
- 对 script 逐步做名字稳定区间分析，把部分 `load_env` / `apply` 收敛成 `load_slot` / `call`
- 在 function 中继续推进静态 `MFunction` 调用、函数内联和纯局部 slot 的寄存器化 / SSA 提升
- 梳理 `eval`、`evalin`、`assignin`、`clear`、路径变化等对环境稳定性的影响
- 为 bytecode lowering、runtime effect summary、world/workspace epoch 设计补足语义接口
- 为后续 profile、guard、热点优化和 `typed SSA` 做准备

从设计角度看，当前仓库仍然处在“先把语义边界和对象模型打稳”的阶段，而不是“全面铺开优化”的阶段。

## 相关文档

如果需要看更细的设计说明，可以从这些文档开始：

- [IR 草案](./doc/ir_draft.md)
- [IR Schema](./doc/ir_schema.md)
- [IR Builder Design](./doc/ir_builder_design.md)
- [IR Lowering Design](./doc/ir_lowering_design.md)
- [循环 Lowering 设计](./doc/loop_lowering_design.md)
- [Switch Lowering 设计](./doc/switch_lowering_design.md)
- [CFG DOT 输出设计](./doc/cfg_dot_design.md)
- [Local 函数支持方案](./doc/local_function_support_design.md)
- [ValueId 类型事实与函数分派设计](./doc/value_type_dispatch_design.md)
- [计划中与思考中的问题](./doc/planning_notes.md)
- [后续需要学习与确认的问题](./doc/learning_notes.md)
- [Execution Strategy](./doc/execution_strategy.md)
- [更新日志](./doc/update_notes.md)
