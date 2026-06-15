# 文档入口

本文是 `doc/` 目录的总入口。阅读时先以 [IR Schema](./ir_schema.md) 为源码现状，再把 runtime、
workspace、global/persistent 和 deopt 相关文档视为后续设计约束。

## 阅读路线

### 1. 当前 IR 主线

建议先读这一组，建立当前 `.m -> IR -> verify/print/cfg-dot` 的闭环认识：

1. [Matlab IR Schema](./ir_schema.md)：当前 `src/ir` 中实际存在的对象、slot、value 和指令。
2. [Matlab IR 草案](./ir_draft.md)：high-level IR 的分层原则和非目标。
3. [IR Lowering 设计](./ir_lowering_design.md)：当前 lowering 支持的语法和语义边界。
4. [Matlab IR Builder 设计](./ir_builder_design.md)：builder 与 lowering 的职责边界。
5. [IR Verifier 设计](./ir_verifier_design.md)：结构校验范围和 smoke test 关系。
6. [更新日志](./update_notes.md)：当前快照和近期 TODO。

### 2. 已实现特性与工具

- [匿名函数句柄设计](./anonymous_function_handle_design.md)：具名/匿名函数句柄、capture 和
  `value_apply`。
- [循环 Lowering 设计](./loop_lowering_design.md)：`for` / `while`、循环 CFG 和
  `break` / `continue`。
- [Switch Lowering 设计](./switch_lowering_design.md)：`switch / case / otherwise` 的 CFG。
- [CFG DOT 输出设计](./cfg_dot_design.md)：`ir_cfg_dot` 输出、节点和边的显示约定。
- [ValueId 类型事实与函数分派设计](./value_type_dispatch_design.md)：`ValueTable` 类型事实和
  后续分派优化。

### 3. Runtime 与动态语义设计

这一组多数是后续 runtime / bytecode / JIT 设计，不等价于当前源码已经全部实现：

- [M 变量模型设计](./variable_model_design.md)：变量类别、binding liveness 和 `clear` 影响矩阵。
- [M 工作区设计](./workspace_design.md)：workspace 作为 `name -> binding` 视图的语义规则。
- [M 函数栈帧设计](./function_frame_design.md)：函数 frame layout、slot fast path 和动态 env 旁路。
- [M 函数工作区中的静态 slot 与动态 env](./function_workspace_env_design.md)：历史讨论归档，
  有效结论已合并到变量、workspace 和函数 frame 文档。
- [Global / Persistent IR 节点设计](./global_persistent_ir_design.md)：后续
  `global` / `persistent` 专用节点。
- [Matlab 执行策略与 Runtime 机制设计](./execution_strategy.md)：interpreter、baseline JIT、
  optimizing JIT 和 deopt 分层。
- [Deopt 与优化运行时参考资料](./deopt_runtime_references.md)：deopt、stack map、guard 和
  runtime 资料清单。

### 4. 计划与学习资料

- [计划中与思考中的问题](./planning_notes.md)：后续 pass、优化顺序和开放问题。
- [后续需要学习与确认的问题](./learning_notes.md)：需要继续学习或验证的编译/runtime 资料。

### 5. Matlab 语义调研

`doc/matlab/` 下记录 Matlab 行为观察和专题设计：

- [MATLAB `import` 机制笔记](./matlab/matlab_import_notes.md)
- [MATLAB 运算符对 local / import 的分派观察](./matlab/operator_local_import_dispatch_probe.md)
- [`runtime_private_plus_dispatch_probe` 观察记录](./matlab/runtime_private_plus_dispatch_probe.md)
- [MATLAB 运算符常量折叠设计](./matlab/constant_folding_design.md)
- [MATLAB 多输出与 comma-separated list 赋值语义](./matlab/multi_output_assignment_semantics.md)
- [高维数组规约运算设计思考](./matlab/high_dim_reduction_design.md)

## 术语约定

- `ir_schema.md` 是当前源码事实的入口。其他设计文档若讨论“后续”或“建议”，应以该文为现状基线。
- 当前源码中，脚本静态名字进入 `ScriptVar` slot，基础 lowering 生成
  `LoadSlotInst` / `StoreSlotInst`，文本 IR 打印为 `load` / `store`。
- Runtime 文档中的 `load_slot` / `store_slot` 是语义伪代码；对应当前 C++ 节点是
  `LoadSlotInst` / `StoreSlotInst`，文本打印仍是 `load` / `store`。
- Runtime 文档中的 `load_workspace` / `store_workspace` 表示 workspace API 或未来可能引入的
  generic binding access。当前源码没有独立的 `LoadWorkspaceInst` / `StoreWorkspaceInst`。
- `global` / `persistent`、完整 workspace、bytecode、JIT 和 deopt 相关文档多数是设计目标，
  不是当前已完成实现。
