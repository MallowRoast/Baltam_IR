# Unreachable Block Elimination Pass

源码位置：`src/pass/unreachable_block_elimination_pass.cpp`

Pass 名称：`unreachable-block-elimination`

作用域：`IRPassScope::CodeUnit`

## 优化内容

该 pass 从 `CodeUnit::entry_block` 出发，沿 `BasicBlock::successors` 标记可达 block，并删除所有不可达
basic block。

删除 block 后会：

- 清理剩余 block 的 predecessor 边
- 对被删除指令定义的 `ValueId` 清空 `ValueInfo::def`
- 保持 `entry_block` 与可达 block 对象不变

## 安全检查

如果不可达 block 定义的 `ValueId` 仍被可达 block 使用，pass 会跳过删除并发出 warning。

这种情况表示 CFG / value use 已经不自洽，直接删除会制造悬空 use。

## 依赖

该 pass 只依赖 `BasicBlock::successors`，因此调用前最好先确保 CFG 边与 terminator 一致。
`CFGSimplificationPass` 会先 normalize CFG edges，再调用本 pass。

## 推荐使用场景

- CFG 改写后清理死 block
- 死分支消除后清理 unreachable path
- `CFGSimplificationPass` 的子步骤
