# Baltam_IR

`Baltam_IR` 当前处于 IR 前端和中端基础设施搭建阶段。

当前仓库的实际主线是：

`M 源码 -> AST -> non-SSA IR -> print`

目前已经落地的部分：

- 统一的 IR 头文件：[src/ir/ir.h](/home/zj/Desktop/Baltam_IR/src/ir/ir.h)
- 显式 CFG 容器：
  - `Module`
  - `Function`
  - `BasicBlock`
- non-SSA 节点体系：
  - `IRNode`
  - `NonSSANode`
  - `SSANode` 占位基类
  - `NumberNode`
  - `TextNode`
  - `AssignNode`
  - `UnaryOpNode`
  - `BinOpNode`
  - `CallNode`
  - `CondJumpNode`
  - `JumpNode`
  - `ReturnNode`
- AST lowering：
  - [src/lowering/lowering.cpp](/home/zj/Desktop/Baltam_IR/src/lowering/lowering.cpp)
- LLVM 风格文本打印：
  - [src/ir/ir_printer.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir_printer.cpp)
- 最小 verifier：
  - [src/analysis/verifier.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/verifier.cpp)
- AnalysisManager / PassManager 骨架：
  - [src/analysis/analysis_manager.h](/home/zj/Desktop/Baltam_IR/src/analysis/analysis_manager.h)
  - [src/optimizer/pass_manager.h](/home/zj/Desktop/Baltam_IR/src/optimizer/pass_manager.h)

当前没有启用的主线：

- 旧 hybrid/value-based IR
- 旧 IR 解释器
- 任何正式优化 pass
- SSA 构建
- LLVM IR lowering

## 当前 IR

当前 IR 是显式 CFG 的 non-SSA IR。

它的特点是：

- 值以 `NamedValue` 传递
- 一个源码名字可以被多次定义
- `BasicBlock` 里是线性节点序列
- `terminal` 只允许：
  - `CondJumpNode`
  - `JumpNode`
  - `ReturnNode`
- 节点保留 `SourceLocation`

这层 IR 的定位是：

- 承接 AST lowering
- 作为后续 analysis 和 SSA 构建的输入
- 作为调试和验证的第一层中间表示

## 构建

```bash
cmake -S . -B build
cmake --build build
```

## 运行

当前 `main` 只做一件事：解析 `.m` 文件并打印 non-SSA IR。

```bash
./build/main simple_demo
./build/main test1
./build/main test1_2
./build/main test1_3
./build/main test1_4
./build/main test1_5
```

如果需要运行时库路径，当前常用方式是：

```bash
LD_LIBRARY_PATH="$PWD/deps/core/lib:/opt/Baltamatica/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}" ./build/main simple_demo
```

## 当前支持的 lowering 范围

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

## 当前仓库布局

- [src/ir/ir.h](/home/zj/Desktop/Baltam_IR/src/ir/ir.h)
  当前唯一正式 IR 定义
- [src/ir/ir.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir.cpp)
  IR 实现
- [src/ir/ir_printer.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir_printer.cpp)
  IR 文本打印
- [src/lowering/lowering.cpp](/home/zj/Desktop/Baltam_IR/src/lowering/lowering.cpp)
  AST 到 non-SSA IR 的 lowering
- [src/analysis/verifier.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/verifier.cpp)
  当前 verifier
- [src/analysis/analysis_manager.h](/home/zj/Desktop/Baltam_IR/src/analysis/analysis_manager.h)
  analysis 缓存框架
- [src/optimizer/pass_manager.h](/home/zj/Desktop/Baltam_IR/src/optimizer/pass_manager.h)
  pass manager 骨架

## 下一步

当前最合理的路线是：

1. 稳定 non-SSA IR 和 verifier
2. 增加 5 个基础 analysis：
   - CFGAnalysis
   - DominatorTree
   - DominanceFrontier
   - Liveness
   - DefUse
3. 从 non-SSA IR 构建 untyped SSA IR
4. 在 SSA IR 上接优化和 profile
5. 再考虑 typed SSA IR 和 LLVM IR lowering
