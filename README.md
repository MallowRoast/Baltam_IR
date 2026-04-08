# Baltam_IR

`Baltam_IR` 当前处于 IR 前端、中端和 SSA 执行基础设施并行落地阶段。

当前仓库已经跑通的主线是：

`M 源码 -> AST -> non-SSA IR -> verify -> analyses -> untyped SSA IR -> verify -> print`

另外，测试里已经覆盖了一条 SSA 执行链：

`parse -> lower(non-SSA) -> verify -> construct_untyped_ssa -> verify -> execute(UntypedSSA)`

## 当前已落地的部分

- 统一的 IR 定义：
  - [src/ir/ir.h](/home/zj/Desktop/Baltam_IR/src/ir/ir.h)
- 显式 CFG 容器：
  - `Module`
  - `Function`
  - `BasicBlock`
- non-SSA 节点体系：
  - `NumberNode`
  - `TextNode`
  - `AssignNode`
  - `UnaryOpNode`
  - `BinOpNode`
  - `CallNode`
  - `CondJumpNode`
  - `JumpNode`
  - `ReturnNode`
- untyped SSA 节点体系：
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
- AST lowering：
  - [src/lowering/lowering.cpp](/home/zj/Desktop/Baltam_IR/src/lowering/lowering.cpp)
- 5 个基础 analysis：
  - [src/analysis/cfg_analysis.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/cfg_analysis.cpp)
  - [src/analysis/dominator_tree.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/dominator_tree.cpp)
  - [src/analysis/dominance_frontier.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/dominance_frontier.cpp)
  - [src/analysis/liveness.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/liveness.cpp)
  - [src/analysis/def_use.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/def_use.cpp)
- non-SSA 到 untyped SSA 的构建器：
  - [src/optimizer/construct_untyped_ssa.cpp](/home/zj/Desktop/Baltam_IR/src/optimizer/construct_untyped_ssa.cpp)
- LLVM 风格文本打印：
  - [src/ir/ir_printer.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir_printer.cpp)
- 分阶段 verifier：
  - [src/analysis/verifier.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/verifier.cpp)
- 面向 `UntypedSSA` 的解释器：
  - [src/interpreter/interpreter.cpp](/home/zj/Desktop/Baltam_IR/src/interpreter/interpreter.cpp)
- AnalysisManager / PassManager 骨架：
  - [src/analysis/analysis_manager.h](/home/zj/Desktop/Baltam_IR/src/analysis/analysis_manager.h)
  - [src/optimizer/pass_manager.h](/home/zj/Desktop/Baltam_IR/src/optimizer/pass_manager.h)

## 当前还没完成或没接入主入口的部分

- `TypedSSA`
- 正式优化 pass
- profile 基础设施
- LLVM IR lowering / JIT
- CLI 级别的 `execute` 入口

## 当前 IR

当前仓库已经在同一套容器层上同时使用两种 IR stage：

- `NonSSA`
  - 用 `NamedValue` 传递源码变量名和 lowering 临时量名
  - 仍允许同名值被多次定义
  - 是 lowering 的直接输出
- `UntypedSSA`
  - 用 `ValueId / ValueRef` 表达显式值流
  - `phi` 放在 `BasicBlock::phi_nodes()`
  - 是当前中端分析之后的统一执行和打印对象

容器层 `Module / Function / BasicBlock` 在两个 stage 之间复用。

当前职责边界也已经比较明确：

- lowering 只负责生成 `NonSSA`
- `optimizer::construct_untyped_ssa_module(...)` 负责把 `NonSSA` 重写成新的 `UntypedSSA` 模块
- verifier 和 printer 同时支持这两个 stage
- interpreter 目前只执行 `UntypedSSA`

## 构建

```bash
cmake -S . -B build
cmake --build build
```

## 运行

当前 `main` 会做这些事情：

1. 解析 `.m` 文件
2. 生成 non-SSA IR
3. 做 verifier
4. 构建 untyped SSA IR
5. 再做 verifier
6. 打印两份 IR
7. 若入口函数是零参数，或通过 `--` 提供了对应个数的字符串实参，则执行 untyped SSA 并打印输出值

示例：

```bash
./build/main simple_demo
./build/main test1 -- left right
./build/main /home/zj/Desktop/Baltam_IR/test/test37/test37.m
```

如果需要运行时库路径，当前常用方式是：

```bash
LD_LIBRARY_PATH="$PWD/deps/core/lib:/opt/Baltamatica/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ./build/main simple_demo
```

当前 `main` 已接入入口函数的 untyped SSA 执行；
零参数入口函数可直接运行，带参数入口函数可通过 `--` 后追加字符串实参，例如
`./build/main test1 -- left right`。
若未提供所需参数，则当前仍只打印 IR，并提示跳过执行。

## 测试

打开 `BUILD_TEST=ON` 后，可直接运行：

```bash
ctest --test-dir build
```

当前和 IR 主线最相关的测试包括：

- [test/construct_untyped_ssa_test.cpp](/home/zj/Desktop/Baltam_IR/test/construct_untyped_ssa_test.cpp)
- [test/interpreter_test.cpp](/home/zj/Desktop/Baltam_IR/test/interpreter_test.cpp)
- [test/simple_demo_test.cpp](/home/zj/Desktop/Baltam_IR/test/simple_demo_test.cpp)

其中 `simple_demo_test` 覆盖的是：

`parse -> lower -> verify -> construct_untyped_ssa -> verify -> execute`

## 当前支持的 lowering / 执行范围

当前 non-SSA lowering 已覆盖一批基础语法：

- 赋值
- 一元和二元表达式
- 直接调用和间接调用
- `if / elseif / else`
- `switch / case / otherwise`
- `for`
- `while`
- `break`
- `continue`
- 匿名函数
- 横向/纵向列表
- 元胞字面量

当前 `for` 语义通过运行时 helper 表达：

- `foreach_init`
- `foreach_iterate`

当前 `UntypedSSA` 解释器已支持执行：

- 常量、文本、`undef`
- `copy`
- 一元/二元运算
- `phi`
- 条件跳转、无条件跳转、返回
- 直接调用和间接调用
- 一部分 helper / 运行时入口：
  - `__ir_make_cell__`
  - `__ir_make_function_handle__`
  - `__ir_switch_match__`

## 当前仓库布局

- [src/ir/ir.h](/home/zj/Desktop/Baltam_IR/src/ir/ir.h)
  当前唯一正式 IR 定义
- [src/ir/ir.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir.cpp)
  IR 实现
- [src/ir/ir_printer.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir_printer.cpp)
  non-SSA / untyped SSA 打印器
- [src/lowering/lowering.cpp](/home/zj/Desktop/Baltam_IR/src/lowering/lowering.cpp)
  AST 到 non-SSA IR 的 lowering
- [src/analysis/verifier.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/verifier.cpp)
  分阶段 verifier
- [src/optimizer/construct_untyped_ssa.cpp](/home/zj/Desktop/Baltam_IR/src/optimizer/construct_untyped_ssa.cpp)
  non-SSA 到 untyped SSA 的构建器
- [src/interpreter/interpreter.cpp](/home/zj/Desktop/Baltam_IR/src/interpreter/interpreter.cpp)
  `UntypedSSA` 解释器
- [src/analysis/analysis_manager.h](/home/zj/Desktop/Baltam_IR/src/analysis/analysis_manager.h)
  analysis 缓存框架
- [src/optimizer/pass_manager.h](/home/zj/Desktop/Baltam_IR/src/optimizer/pass_manager.h)
  pass manager 骨架

## 下一步

当前更合理的路线是：

1. 在 `UntypedSSA` 上补正式优化 pass
2. 扩大解释器覆盖范围和端到端测试，并决定是否给 CLI 加执行入口
3. 明确 `TypedSSA` 和 profile 方案
4. 再进入 LLVM IR lowering / JIT
