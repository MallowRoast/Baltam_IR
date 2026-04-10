# Baltam_IR JIT 方案整理

## 当前结论

JIT 仍然不是当前阶段的下一步。

当前仓库已经走到了：

`non-SSA IR -> analyses -> untyped SSA IR -> verify -> print / execute`

但离适合进入 JIT 还有明显距离。

## 当前已经具备什么

当前已经有：

- AST parsing
- non-SSA lowering
- 5 个基础 analysis
- untyped SSA 节点体系
- non-SSA 到 untyped SSA 的构建器
- 分阶段 verifier
- untyped SSA 打印
- untyped SSA 解释器

也就是说，当前缺的已经不再是“IR 地基”。

## 当前还缺什么

当前真正还缺的是：

- 正式优化 pass 管线
- profile 基础设施
- `TypedSSA`
- LLVM IR lowering
- JIT runtime / codegen 集成

## JIT 的推荐定位

JIT 更适合作为 `TypedSSA` 之后的热点后端，而不是当前项目的主 IR。

建议定位仍然是：

`TypedSSA -> LLVM IR -> native code`

而不是：

`AST -> 直接 LLVM/MLIR`

## 为什么现在还不建议做 JIT

原因已经从“没有 SSA”变成了：

- 还没有正式优化 pass
- 还没有 profile
- 还没有类型专门化
- 还没有 LLVM lowering

如果现在直接进入 JIT，只会把调试难度转移到更低层，而不会减少上层 IR 的不确定性。

## 真正进入 JIT 前的前置条件

至少应具备：

1. 稳定的 `UntypedSSA` 优化管线
2. 可回归的解释执行和端到端测试
3. profile 数据采集方案
4. `TypedSSA` 设计和实现
5. LLVM IR lowering
6. JIT runtime / codegen 集成

## 当前阶段更值得做的事

当前更值得投入的是：

- 扩充 `UntypedSSA` 解释器与回归测试
- 在 `UntypedSSA` 上增加正式优化 pass
- 设计 profile 如何附着到 IR
- 明确 `TypedSSA` 的 specialization 边界

这些工作完成后，JIT 才会有稳定输入，而不是变成另一个承压层。
