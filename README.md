# Baltam_IR

`Baltam_IR` 当前维护的是一条以 `NonSSA` 和 `UntypedSSA` 为中心的 IR 主线：

`M 源码 -> AST -> NonSSA -> verify -> analyses -> UntypedSSA -> verify -> print / execute`

目前仓库已经具备：

- AST 到 `NonSSA` 的 lowering
- 显式 CFG 容器与分阶段 verifier
- `CFG / DominatorTree / DominanceFrontier / Liveness / DefUse`
- `NonSSA -> UntypedSSA` 构建
- non-SSA / untyped SSA 文本打印
- 面向 `UntypedSSA` 的解释执行

## 核心模块

- [src/ir/ir.h](/home/zj/Desktop/Baltam_IR/src/ir/ir.h)
  统一 IR 定义，包含 `NonSSA` 与 `UntypedSSA`
- [src/lowering/lowering.cpp](/home/zj/Desktop/Baltam_IR/src/lowering/lowering.cpp)
  AST 到 non-SSA 的 lowering
- [src/analysis](/home/zj/Desktop/Baltam_IR/src/analysis)
  CFG 与名字级 analysis、verifier
- [src/optimizer/construct_untyped_ssa.cpp](/home/zj/Desktop/Baltam_IR/src/optimizer/construct_untyped_ssa.cpp)
  `NonSSA -> UntypedSSA`
- [src/interpreter/interpreter.cpp](/home/zj/Desktop/Baltam_IR/src/interpreter/interpreter.cpp)
  `UntypedSSA` 解释器
- [src/ir/ir_printer.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir_printer.cpp)
  IR 打印器

## 当前范围

当前已经覆盖的主要语义包括：

- 基础赋值、常量、文本、一元/二元表达式
- 直接调用、间接调用、匿名函数
- `if / elseif / else`
- `switch / case / otherwise`
- `for / while / break / continue`
- 短路逻辑
- cell 字面量、cell 取值与写回
- 圆括号索引写回与运行时分派
- `varargin / varargout / nargin / nargout`
- `end` 在索引表达式中的 lowering 与执行

当前仍未完成或未接入主入口的部分包括：

- `TypedSSA`
- 正式优化 pass 管线
- profile 基础设施
- LLVM IR lowering / JIT
- CLI 级别的执行入口

## 构建

```bash
cmake -S . -B build
cmake --build build
```

主产物是库目标 `BALTAM_IR`。测试打开后可运行：

```bash
ctest --test-dir build
```

如果只验证单个脚本回归，可直接运行对应测试，例如：

```bash
ctest --test-dir build --output-on-failure -R '^test_test8$'
```

## 作为库使用

当前推荐直接按阶段接口组合：

1. `bt_ast_interface::parse_mfile(...)`
2. `lower_parsed_units_to_ir(...)`
3. `analysis::verify_module_or_throw(...)`
4. `optimizer::construct_untyped_ssa_module(...)`
5. `analysis::verify_module_or_throw(...)`

调用方在进入这条链路前仍需先执行 `bt_ast_interface::initialize()`，结束后执行
`bt_ast_interface::finalize()`。

## 文档

`README` 只保留仓库总览。专题说明放在 [doc](/home/zj/Desktop/Baltam_IR/doc)：

- analysis / verifier / SSA / interpreter 的现状说明
- 短路逻辑、可变参数等专题设计记录
- `UntypedSSA` 优化与 JIT 的后续方向

## 已知边界

- `test3` 仍依赖 runtime 工作区语义，当前解释器没有完整同步这套模型
- `test7` 仍缺复杂左值与短路组合路径的 lowering
- 一部分文档仍保留历史设计背景，但已去掉与 `README` 重复的总览信息
