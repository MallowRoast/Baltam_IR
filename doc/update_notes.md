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

3. 增加对 local 函数的最小支持，见 `test0_2` 和 `test0_3`。

具体包括：

- 脚本中的 local 函数调用目前仍保留为 `apply`
- 函数中的 local 函数调用可以静态分派
- 支持一元 / 二元运算符对 local 函数的分派
- 验证了函数中局部变量对 local 函数的遮蔽作用

TODO：

- 增加 `parent_get` 节点
- 支持 `for / while / switch` 及其嵌套情况
