# 更新日志

## v0.0.1

1. 完成 IR 的基础设计，主要包括 IR 本身，以及 `builder`、`lowering`、`printer` 三部分。
2. 实现对简单脚本和函数的 lowering，见 `test0` 和 `test0_1`。

目前支持：

- 脚本变量的 `load_workspace` / `store_workspace`
- 脚本中的 `Apply` 节点
- `if / else / end` 语句的 lowering
- 函数中变量的 `load_slot` / `store_slot`
- 函数中 `Apply` 节点到 `Call` 节点的降级

3. 增加对 local 函数的最小支持，见 `test1` 和 `test1_1`。

具体包括：

- 脚本中的 local 函数调用目前仍保留为 `apply`
- 函数中的 local 函数调用可以静态分派
- 支持一元 / 二元运算符对 local 函数的分派
- 验证了函数中局部变量对 local 函数的遮蔽作用
- MATLAB 不允许脚本变量与同文件 local 函数同名；因此不保留这类脚本遮蔽测试输入

4. 增加第一版 IR verifier，见 [ir_verifier_design.md](/home/zj/Desktop/Baltam_IR/doc/ir_verifier_design.md)。

当前 verifier 覆盖：

- `MFileUnit / CodeUnit / FunctionUnit` 的 parent 与入口引用
- slot 表和 hidden slot 约束
- block terminator 约束
- CFG predecessor / successor 与 terminator 目标的一致性
- `ValueId / SlotId / Operand` 引用合法性
- `CallInst` 的 direct / local / indirect callee 约束

5. 增加第一版 IR type 表示，见 `src/ir/ir_type.h`。

当前 type 层覆盖：

- 使用 `TypeSet` 的静态成员构造入口表示当前已知的叶子类型和常用分类，包括 logical、整数、浮点/复数、文本、容器和 function handle
- 使用 `std::bitset`-backed `TypeSet` 表示 bottom、any、单一类型和 union type
- 提供 `join` / `meet` / subset / `maybe` / `definitely` 等基础集合操作
- 提供 numeric、text、container、callable 等分类 helper，分类本身不作为独立 atom 存在
- 提供稳定调试名和 `TypeSet` 输出格式，便于 smoke test、printer、verifier 和后续类型分析共享

6. 增加 `for / while` 循环 lowering，见 [loop_lowering_design.md](/home/zj/Desktop/Baltam_IR/doc/loop_lowering_design.md)。

当前支持：

- 支持简单 `for` 循环，例如 `for i = 1:10 ... end`
- 支持 `for` 循环体内的 `break / continue`
- 支持嵌套 `for` 循环，以及嵌套循环中的 `break / continue`
- 支持简单 `while` 循环，例如 `while i < 10 ... end`
- 支持 `while` 循环体内的 `break / continue`
- 支持 `for / while` 相互嵌套，以及混合嵌套中的 `break / continue`

7. 增加 CFG DOT 输出工具，见 [cfg_dot_design.md](/home/zj/Desktop/Baltam_IR/doc/cfg_dot_design.md)。

当前支持：

- `ir_cfg_dot <input.m> -o <output.dot>` 生成单个 `.m` 文件对应的 Graphviz DOT
- 可选 `--svg` / `--png` 通过 Graphviz `dot` 渲染图片
- `generate_cfg_dot` 构建目标会为 `test/m` 下当前语法用例生成 DOT 到 `build/test/cfg_dot`

8. 增加 `switch / case / otherwise` lowering，见 [switch_lowering_design.md](/home/zj/Desktop/Baltam_IR/doc/switch_lowering_design.md)。

当前支持：

- `switch` 表达式只 lower 一次
- 支持普通 `case` 和 cell 形式 `case {a, b}`
- 支持 `otherwise`
- 支持没有 `otherwise` 的 `switch`
- 支持 `switch` 嵌套
- 支持 `for / while` 循环中嵌套 `switch`，以及 `switch case` 包裹 `for / while`
- 支持 `ir_print <input.m> -o <output.ir>` 打印文本 IR 文件

9. Stage 保存：增加具名函数句柄和 `ValueApply` 相关 IR 支持。

当前支持：

- 新增 `CreateNamedFunctionHandleInst`，用于表达源码层 `@name` 的具名函数句柄构造
- 具名函数句柄构造区分运行时 lookup 和静态 prebound，结果类型固定为 `function_handle`
- 新增 `ValueApplyInst`，将已知 base 为运行时值的圆括号应用统一表达为 `value_apply`
- 函数中已绑定变量的 `f(...)` 会 lower 为 `load_slot f` + `value_apply`
- 矩阵变量下标读取 `A(...)` 同样 lower 为 `load_slot A` + `value_apply`
- `ApplyInst` 继续保留脚本名字应用等尚未消歧的 `A(...)`
- 新增 `test5` 覆盖具名函数句柄创建/调用、矩阵创建、下标读取和下标写入
- 增加匿名函数句柄设计问题记录，见 [anonymous_function_handle_design.md](/home/zj/Desktop/Baltam_IR/doc/anonymous_function_handle_design.md)

TODO：

- 增加 `parent_get` 节点
- 补齐 `for` 的更完整迭代协议
- 第一阶段：补齐完整下标语法，包括 `A(:, 2)`、`A(end, :)`、`A{1}`、`S.field`
  和 `A(1).x{2}` 等链式访问
- 第一阶段：补齐更多字面量，包括 `[]`、`"abc"`、`'abc'`、`true / false`、
  cell literal 和 struct 相关构造
- 第一阶段：落地匿名函数句柄 lowering，包括 `@(x) x + y`、捕获值和
  `create_anon_func` 设计
- 第一阶段：补齐多返回值与左值列表，包括 `[a, b] = f(x)`、`[~, b] = f()`
  和 placeholder 处理
- 第二阶段：支持 `global` / `persistent`，并接入变量 lookup 与 slot/env 语义
- 第二阶段：支持短路逻辑 `&&` / `||`，使用 CFG 表达条件求值
