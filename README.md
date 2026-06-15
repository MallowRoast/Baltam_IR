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
- 短路逻辑 `&& / ||`，通过 CFG 表达右侧按需求值
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

阅读时先以 [IR Schema](./doc/ir_schema.md) 为源码现状，再把 runtime、workspace、
global/persistent 和 deopt 相关文档视为后续设计约束。

### 当前 IR 主线

建议先读这一组，建立当前 `.m -> IR -> verify/print/cfg-dot` 的闭环认识：

1. [Matlab IR Schema](./doc/ir_schema.md)：当前 `src/ir` 中实际存在的对象、slot、value 和指令。
2. [Matlab IR 草案](./doc/ir_draft.md)：high-level IR 的分层原则和非目标。
3. [IR Lowering 设计](./doc/ir_lowering_design.md)：当前 lowering 支持的语法和语义边界。
4. [Matlab IR Builder 设计](./doc/ir_builder_design.md)：builder 与 lowering 的职责边界。
5. [IR Verifier 设计](./doc/ir_verifier_design.md)：结构校验范围和 smoke test 关系。
6. [更新日志](./doc/update_notes.md)：当前快照和近期 TODO。

### 已实现特性与工具

- [匿名函数句柄设计](./doc/anonymous_function_handle_design.md)：具名/匿名函数句柄、capture 和
  `value_apply`。
- [循环 Lowering 设计](./doc/loop_lowering_design.md)：`for` / `while`、循环 CFG 和
  `break` / `continue`。
- [Switch Lowering 设计](./doc/switch_lowering_design.md)：`switch / case / otherwise` 的 CFG。
- `&& / ||` 短路逻辑已在 [IR Lowering 设计](./doc/ir_lowering_design.md) 中记录，当前通过
  CFG 与内部 logical 结果 slot 表达按需求值。
- [CFG DOT 输出设计](./doc/cfg_dot_design.md)：`ir_cfg_dot` 输出、节点和边的显示约定。
- [ValueId 类型事实与函数分派设计](./doc/value_type_dispatch_design.md)：`ValueTable` 类型事实和
  后续分派优化。

### Runtime 与动态语义设计

这一组多数是后续 runtime / bytecode / JIT 设计，不等价于当前源码已经全部实现：

- [M 变量模型设计](./doc/variable_model_design.md)：变量类别、binding liveness 和 `clear` 影响矩阵。
- [M 工作区设计](./doc/workspace_design.md)：workspace 作为 `name -> binding` 视图的语义规则。
- [M 函数栈帧设计](./doc/function_frame_design.md)：函数 frame layout、slot fast path 和动态 env 旁路。
- [M 函数工作区中的静态 slot 与动态 env](./doc/function_workspace_env_design.md)：历史讨论归档，
  有效结论已合并到变量、workspace 和函数 frame 文档。
- [Global / Persistent IR 节点设计](./doc/global_persistent_ir_design.md)：后续
  `global` / `persistent` 专用节点。
- [Matlab 执行策略与 Runtime 机制设计](./doc/execution_strategy.md)：interpreter、baseline JIT、
  optimizing JIT 和 deopt 分层。
- [Deopt 与优化运行时参考资料](./doc/deopt_runtime_references.md)：deopt、stack map、guard 和
  runtime 资料清单。

### 计划与学习资料

- [计划中与思考中的问题](./doc/planning_notes.md)：后续 pass、优化顺序和开放问题。
- [InternalLocal 复用 Pass 设计](./doc/internal_local_reuse_pass_design.md)：内部临时 slot 的
  live range、物理 frame 复用和短路结果 slot 复用边界。
- [后续需要学习与确认的问题](./doc/learning_notes.md)：需要继续学习或验证的编译/runtime 资料。

### Matlab 语义调研

`doc/matlab/` 下记录 Matlab 行为观察和专题设计：

- [MATLAB `import` 机制笔记](./doc/matlab/matlab_import_notes.md)
- [MATLAB 运算符对 local / import 的分派观察](./doc/matlab/operator_local_import_dispatch_probe.md)
- [`runtime_private_plus_dispatch_probe` 观察记录](./doc/matlab/runtime_private_plus_dispatch_probe.md)
- [MATLAB 运算符常量折叠设计](./doc/matlab/constant_folding_design.md)
- [MATLAB 多输出与 comma-separated list 赋值语义](./doc/matlab/multi_output_assignment_semantics.md)
- [高维数组规约运算设计思考](./doc/matlab/high_dim_reduction_design.md)

### 术语约定

- `ir_schema.md` 是当前源码事实的入口。其他设计文档若讨论“后续”或“建议”，应以该文为现状基线。
- 当前源码中，脚本静态名字进入 `ScriptVar` slot，基础 lowering 生成
  `LoadSlotInst` / `StoreSlotInst`，文本 IR 打印为 `load` / `store`。
- Runtime 文档中的 `load_slot` / `store_slot` 是语义伪代码；对应当前 C++ 节点是
  `LoadSlotInst` / `StoreSlotInst`，文本打印仍是 `load` / `store`。
- Runtime 文档中的 `load_workspace` / `store_workspace` 表示 workspace API 或未来可能引入的
  generic binding access。当前源码没有独立的 `LoadWorkspaceInst` / `StoreWorkspaceInst`。
- `global` / `persistent`、完整 workspace、bytecode、JIT 和 deopt 相关文档多数是设计目标，
  不是当前已完成实现。
