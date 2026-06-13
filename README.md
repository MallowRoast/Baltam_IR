# Baltam IR

`Baltam_IR` 是面向 `M` 语言 / Matlab 风格语言的 high-level IR 实验仓库。当前重点是把
脚本、函数、slot、动态 env、调用分派和控制流等高层动态语义稳定落到 IR，而不是过早把
整门语言压成全局 SSA。

当前设想的主流水线是：

```text
AST -> IR -> bytecode -> interpreter/profile -> typed SSA -> LLVM IR
```

## 当前状态

仓库已经形成 `.m -> IR -> verify/print/cfg-dot` 的最小闭环：

- `IRModule` 拥有 `.m` 文件单元和 module 级匿名函数表
- `ScriptUnit`、`FunctionUnit`、`AnonymousFunctionUnit`
- `IRBuilder`、`IRLowerer`、`IRPrinter`、`IRVerifier`
- `ValueTable` 和基础类型事实
- `ir_print` 文本 IR 工具
- `ir_cfg_dot` CFG DOT / SVG / PNG 工具

当前 lowering 覆盖：

- 脚本变量静态编号为 `ScriptVar` slot，文本 IR 打印为 `load` / `store`
- 函数变量、参数、返回值和捕获值同样通过 slot 访问，文本 IR 打印为 `load` / `store`
- `apply`、`value_apply`、`call`
- 文件内 local 函数单元收集；调用在基础 lowering 中先保持动态 direct call
- 具名函数句柄、匿名函数句柄
- 多返回值调用和 `[~, b] = f(...)` 占位输出位
- `if / else`
- `switch / case / otherwise`
- `for / while`，包括嵌套循环和 `break / continue`
- 显式 `return` 与隐式 `return`

当前还未完整覆盖：

- REPL / `CommandUnit` lowering
- 嵌套函数和完整闭包 runtime
- `try / catch`
- `global` / `persistent`
- 完整索引、成员访问、`magic_end`
- 完整 Matlab 函数优先级、路径和 import 语义

## 快速查看

构建后打印文本 IR：

```bash
./build/ir_print test/m/test4/test4.m -o /tmp/test4.ir
```

生成 CFG DOT 或图片：

```bash
./build/ir_cfg_dot test/m/test4/test4.m -o /tmp/test4.dot --svg /tmp/test4.svg
```

运行 smoke test：

```bash
ctest --test-dir build --output-on-failure -L smoke
```

如果本机 runtime / builtin 动态库加载失败，按 smoke test 使用的环境补充：

```bash
env HOME=/tmp XDG_CACHE_HOME=/tmp/.cache BALTAM_FRONTEND=console \
  LD_LIBRARY_PATH=/opt/Baltamatica/lib \
  ./build/ir_print test/m/test4/test4.m -o /tmp/test4.ir
```

## 文档入口

建议按这个顺序阅读：

1. [IR Schema](./doc/ir_schema.md)：当前源码里的 IR 对象和指令 schema
2. [IR Lowering 设计](./doc/ir_lowering_design.md)：当前 `.m -> IR` lowering 行为
3. [IR Builder 设计](./doc/ir_builder_design.md)：底层 IR 构造器职责
4. [IR Verifier 设计](./doc/ir_verifier_design.md)：结构校验范围
5. [匿名函数句柄设计](./doc/anonymous_function_handle_design.md)：匿名函数特有语义
6. [计划中与思考中的问题](./doc/planning_notes.md)：后续 pass 和优化方向
7. [更新日志](./doc/update_notes.md)：当前快照和近期 TODO

专题文档：

- [IR 草案](./doc/ir_draft.md)
- [循环 Lowering 设计](./doc/loop_lowering_design.md)
- [Switch Lowering 设计](./doc/switch_lowering_design.md)
- [CFG DOT 输出设计](./doc/cfg_dot_design.md)
- [ValueId 类型事实与函数分派设计](./doc/value_type_dispatch_design.md)
- [Execution Strategy](./doc/execution_strategy.md)
- [后续需要学习与确认的问题](./doc/learning_notes.md)

Matlab 语义调研记录位于 [doc/matlab](./doc/matlab)。
