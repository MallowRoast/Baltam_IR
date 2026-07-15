# 更新日志

## v0.0.1

1. 完成 IR 的基础设计，主要包括 IR 本身，以及 `builder`、`lowering`、`printer` 三部分。
2. 实现对简单脚本和函数的 lowering，见 `test0` 和 `test0_1`。

目前支持：

- 脚本变量的 `ScriptVar` slot 和 `load` / `store`
- 脚本中的 `Apply` 节点
- `if / else / end` 语句的 lowering
- 函数中变量的 `load` / `store`
- 函数中 `Apply` 节点到 `Call` 节点的降级

3. 增加对 local 函数的最小支持，见 `test1` 和 `test1_1`。

具体包括：

- 脚本中的 local 函数调用目前仍保留为 `apply`
- 函数中的未绑定名字调用先保留为动态 direct `call`
- 一元 / 二元运算符在基础 lowering 中保留为 `UnaryInst` / `BinaryInst`
- 验证了函数中局部变量对同名调用的遮蔽作用
- MATLAB 不允许脚本变量与同文件 local 函数同名；因此不保留这类脚本遮蔽测试输入

4. 增加第一版 IR verifier，见 [ir_verifier_design.md](./ir_verifier_design.md)。

当前 verifier 覆盖：

- `IRModule / MFileUnit / CodeUnit / FunctionUnit / AnonymousFunctionUnit` 的所有权与入口引用
- slot 表和 `SlotTag` 约束
- block terminator 约束
- CFG predecessor / successor 与 terminator 目标的一致性
- `ValueId / SlotId / Operand` 引用合法性
- `CallInst` 的 direct / indirect callee 约束，以及 `Internal / MFunction` 分派约束

5. 增加第一版 IR type 表示，见 `src/ir/ir_type.h`。

当前 type 层覆盖：

- 使用 `TypeSet` 的静态成员构造入口表示当前已知的叶子类型和常用分类，包括 logical、
  整数、浮点/复数、文本、容器和 function handle
- 使用 `std::bitset`-backed `TypeSet` 表示 bottom、any、单一类型和 union type
- 提供 `join` / `meet` / subset / `maybe` / `definitely` 等基础集合操作
- 提供 numeric、text、container、callable 等分类 helper，分类本身不作为独立 atom 存在
- 提供稳定调试名和 `TypeSet` 输出格式，便于 smoke test、printer、verifier 和后续类型分析共享

6. 增加 `for / while` 循环 lowering，见 [loop_lowering_design.md](./loop_lowering_design.md)。

当前支持：

- 支持简单 `for` 循环，例如 `for i = 1:10 ... end`
- 支持 `for` 循环体内的 `break / continue`
- 支持嵌套 `for` 循环，以及嵌套循环中的 `break / continue`
- 支持简单 `while` 循环，例如 `while i < 10 ... end`
- 支持 `while` 循环体内的 `break / continue`
- 支持 `for / while` 相互嵌套，以及混合嵌套中的 `break / continue`

7. 增加 CFG DOT 输出工具，见 [cfg_dot_design.md](./cfg_dot_design.md)。

当前支持：

- `ir_cfg_dot <input.m> -o <output.dot>` 生成单个 `.m` 文件对应的 Graphviz DOT
- 可选 `--svg` / `--png` 通过 Graphviz `dot` 渲染图片
- `generate_cfg_dot` 构建目标会为 `test/m` 下当前语法用例生成 DOT 到 `build/test/cfg_dot`

8. 增加 `switch / case / otherwise` lowering，见 [switch_lowering_design.md](./switch_lowering_design.md)。

当前支持：

- `switch` 表达式只 lower 一次
- 支持普通 `case` 和 cell 形式 `case {a, b}`
- 支持 `otherwise`
- 支持没有 `otherwise` 的 `switch`
- 支持 `switch` 嵌套
- 支持 `for / while` 循环中嵌套 `switch`，以及 `switch case` 包裹 `for / while`
- 支持 `ir_print <input.m> -o <output.ir>` 打印文本 IR 文件

9. 增加具名函数句柄、匿名函数句柄和 `ValueApply` 相关 IR 支持。

当前支持：

- 新增 `CreateNamedFunctionHandleInst`，用于表达源码层 `@name` 的具名函数句柄构造
- 具名函数句柄构造默认 runtime 解析；static 绑定留给后续名字解析 pass
- 新增 `ValueApplyInst`，将已知 base 为运行时值的圆括号应用统一表达为 `value_apply`
- 函数中已绑定变量的 `f(...)` 会 lower 为 `load f` + `value_apply`
- 矩阵变量下标读取 `A(...)` 同样 lower 为 `load A` + `value_apply`
- `ApplyInst` 继续保留脚本名字应用等尚未消歧的 `A(...)`
- 新增 `CreateAnonymousFunctionHandleInst` 和 `AnonymousFunctionUnit`
- 匿名函数体由 `IRModule::anonymous_functions` 拥有，普通 IR 通过 `AnonymousFunctionId`
  间接引用
- 匿名函数捕获值由构造点的 `ValueId` 表达，函数体内部通过只读 capture slot 读取
- 新增 `test5_1` 覆盖具名函数句柄、匿名函数句柄和捕获值 lowering
- 增加匿名函数句柄设计记录，见
  [anonymous_function_handle_design.md](./anonymous_function_handle_design.md)

10. 增加 `IRModule` 顶层组织。

当前支持：

- `IRBuildResult` 返回 `IRModule`，并保留当前入口 `MFileUnit*`
- `IRModule` 拥有 `.m` 文件单元和 module 级匿名函数表
- `MFileUnit::module` 指回所属 module
- `CodeUnit` 基类不统一保存 `MFileUnit* parent` 或 `IRModule* module`
- `ScriptUnit` / `FunctionUnit` 保存 `file` 指针
- `AnonymousFunctionUnit` 保存 `lexical_parent`
- `CommandUnit` 仅作为类型占位保留，不接入 lowering、builder、printer 或 verifier

11. 增加多返回值与占位符输出支持。

当前支持：

- 函数签名支持多个返回 slot
- `[a, b] = f(...)` 会生成多结果 `call`
- `[~, b] = f(...)` 保留输出位次，但占位输出位不创建真实 `ValueId`
- printer 把占位输出位显示为 `[]`
- 新增 `test7` 覆盖多返回值和 placeholder lowering

12. 整理文档入口与 runtime 设计文档。

当前文档结构：

- 将根目录 [README](../README.md) 作为唯一文档入口，集中维护阅读路线、专题分类和术语边界。
- 将当前源码事实统一指向 [IR Schema](./ir_schema.md)，runtime / workspace / deopt 文档作为
  后续设计约束阅读。
- 增加变量、workspace、函数 frame、global/persistent、deopt 等 runtime 设计记录：
  [variable_model_design.md](./variable_model_design.md)、
  [workspace_design.md](./workspace_design.md)、
  [runtime_execution_objects_design.md](./runtime_execution_objects_design.md)、
  [global_persistent_ir_design.md](./global_persistent_ir_design.md)、
  [deopt_runtime_references.md](./deopt_runtime_references.md)。
- 明确当前脚本静态名字已经是 `ScriptVar` slot，文本 IR 打印仍是 `load` / `store`；
  runtime 文档中的 `load_workspace/store_workspace` 表示 workspace API 或未来 generic binding
  access，不是当前源码里的独立 IR 节点。

13. 增加短路逻辑 `&& / ||` lowering，见 `test6`。

当前支持：

- `node_logic_and_short` / `node_logic_or_short` 不再 lower 成普通 `BinaryInst And/Or`
- 左侧表达式先求值，再通过 `BranchInst` 决定是否进入右侧求值块
- `&&` 的短路路径写入 logical `false`，`||` 的短路路径写入 logical `true`
- rhs 路径仅在需要时 lower 并写入结果
- 两条路径通过 `InternalLocal` logical slot 在 merge 块汇合，适配当前无 phi 的 IR 形态
- 新增 `test/m/test6/test6.m` 和 `test6_smoke` 覆盖短路 CFG、内部结果 slot 和
  `BinaryInst And/Or` 不应出现

14. 增加 `InternalLocalReusePass` 设计记录。

当前设计：

- pass 不放进基础 lowering；lowering 继续为每个短路表达式创建独立 logical internal slot
- 第一阶段只分析 `InternalLocal logical` 短路临时 slot 的 live range
- 优先产出 logical slot 到 physical frame slot 的映射，供 runtime frame layout 使用
- 暂不默认改变 `ir_print` 输出，避免丢失 canonical lowering 的调试形状
- 详细设计见 [internal_local_reuse_pass_design.md](./internal_local_reuse_pass_design.md)

15. 增加圆括号索引上下文中的基础 `magic_end` lowering，见 `test8`。
详细设计见 [magic_end_design.md](./magic_end_design.md)。

当前支持：

- `ValueApplyInst` 和 `internal.paren_assign` 的索引实参使用单独的索引上下文 lowering
- `node_magic_end` 统一 lower 成 `MagicEndInst`，候选上下文中保留 base、`dim` 和
  `nindices`
- `MagicEndInst` 的结果类型事实保持 `unknown`，不在基础 IR 中假设类自定义 `end` 方法的
  返回类型
- `end - 1`、`1:end` 这类索引表达式会递归携带同一层 base 与维度上下文
- `A(fun(end))` 会保留内层到外层的候选上下文链，例如 `fun -> A`
- 脚本单层 `A(end)` 没有多层候选，但仍生成单候选 `MagicEndInst`
- `MagicEndContext::callee_or_base` 的 Operand 种类保留绑定语义：`InternedString` 是尚未解析
  的名字候选，`Slot` / `ValueId` 是已绑定变量或数据流候选；已绑定候选遇到 cleared /
  unbound slot 应报变量引用错误，不能回退到同名函数或外层候选
- `end` 不按普通函数名解析，用户自定义 `end.m` 不是合法候选；类对象索引中的自定义
  `end` 方法由后续处理 `MagicEndInst` 的 pass / runtime 负责
- 新增 `test/m/test8/test8.m` 和 `test8_smoke` 覆盖单维 `end`、二维第 1/2 维
  `end`、冒号范围里的 `end`、索引赋值里的 `end` 和嵌套索引 `A(fun(end))`
- 新增 `test/m/test8/test8_1.m` 和 `test8_1_smoke` 覆盖脚本里 `A(fun(end))`
  无法静态确定归属层级的场景

16. 收敛解释执行和运行时对象设计。

- 主执行路径调整为 `AST -> high-level IR -> IR interpreter/profile -> typed SSA -> LLVM IR`，
  不在 high-level IR 与 interpreter 之间维护独立的中间执行 IR。
- 定义 `InterpreterContext / CodeObject / Frame` 三个一级运行时对象，分别承载会话级状态、
  冻结代码级状态和单次调用状态。
- builtin 和 plugin 继续复用项目已有查询与调用机制，不在 `InterpreterContext` 或
  `CodeObject` 中重复维护注册表。
- 统一所有权、slot/value layout、persistent/global/capture 存储位置和 IR continuation 规则，
  见 [InterpreterContext、CodeObject 与 Frame 设计](./runtime_execution_objects_design.md)。

TODO：

- 增加 `parent_get` 节点
- 固化 `for` 的 `foreach_init / foreach_iterate` helper ABI，并补齐矩阵按列迭代、
  空迭代源、运行时错误等边界测试。`foreach_iterate` 已负责按 Matlab 规则返回当前
  迭代值，包括矩阵场景下的当前列。
- 第一阶段：补齐完整下标语法，包括 `A(:, 2)`、`A(end, :)`、`A{1}`、`S.field`
  和 `A(1).x{2}` 等链式访问
- 第一阶段：补齐完整 `magic_end` / `end` 上下文语义。当前只覆盖圆括号索引和索引赋值；
  更复杂的嵌套名字解析场景仍需结合函数 / 变量分派结果判断。
- 第一阶段：补齐更多字面量，包括 `[]`、`"abc"`、`'abc'`、`true / false`、
  cell literal 和 struct 相关构造
- 第二阶段：支持 `global` / `persistent`，并接入变量 lookup 与 slot/env 语义
