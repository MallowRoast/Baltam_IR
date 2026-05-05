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

TODO：

- 增加 `parent_get` 节点
- 支持 `for / while / switch` 及其嵌套情况
