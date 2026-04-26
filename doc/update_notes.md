# 更新日志

## v0.0.1

1. 完成了 IR 的基础设计，主要包括 IR 本身，以及 builder、lowering、printer 这三部分。
2. 实现了对简单脚本和函数的 lowering，见 `test0` 和 `test0_1`。

目前支持：

- 脚本变量的 `load_workspace` / `store_workspace`
- 脚本中的 `Apply` 节点
- `if / else / end` 语句的 lowering
- 函数中变量的 `load_slot` / `store_slot`
- 函数中 `Apply` 节点到 `Call` 节点的降级

TODO：

- 支持脚本 / 函数中的 `local` 函数

## v0.0.2

1. 新增仓库根目录 `README.md`，补充仓库目标、当前能力和后续计划说明。
2. 整理设计笔记，合并并改名为 `doc/planning_notes.md`。
3. 调整测试样例布局：
   `test0` 保留在 `test/m/test0/test0.m`，
   `test0_1` 移到 `test/test0_1.m`。
4. 删除 `test/m/README.md`，将测试说明直接写入对应 `.m` 文件头部注释。
5. 统一 smoke test 的运行时基线到 `/opt/Baltamatica/lib`，`deps/` 目录只保留头文件用途。

当前状态：

- `cmake --build build` 可通过
- `ctest --test-dir build` 可通过
