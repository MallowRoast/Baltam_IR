# IR 优化 Pass 文档

本文档目录记录已经实现的 high-level IR 优化 pass。PassManager 调度机制见
[IR PassManager 设计](../ir_pass_manager_design.md)。

当前已实现：

- [Constant Deduplication Pass](./constant_deduplication_pass.md)
- [Constant Folding Pass](./constant_folding_pass.md)
- [Dead Branch Elimination Pass](./dead_branch_elimination_pass.md)
- [Load Forwarding Pass](./load_forwarding_pass.md)
- [Unreachable Block Elimination Pass](./unreachable_block_elimination_pass.md)
- [CFG Simplification Pass](./cfg_simplification_pass.md)

这些 pass 的共同边界：

- 不改变 Matlab 动态语义。
- 不把未解析的 dynamic call/operator 当作纯 primitive。
- 默认运行在 high-level IR 上，而不是 typed SSA。
- pass 改写后应通过 `IRVerifier`。

当前工具 `ir_print --run-passes` 使用的默认 cleanup pipeline 是：

```text
constant-deduplication
load-forwarding
constant-folding
dead-branch-elimination
constant-deduplication
cfg-simplification
```

其中 `cfg-simplification` 内部会调用 unreachable block elimination。
