# Baltam_IR JIT 方案整理

## 目标

本文档整理当前项目中 JIT 的合理定位，以及在真正做热点编译之前还需要补齐哪些前置基础设施。

它不再沿用早期基于旧 IR 节点形状的长篇设想，而以当前仓库的实际状态为准。

## 当前判断

当前项目最合适的主线仍然是：

`AST -> 自研 CFG IR -> Verifier/Optimize -> IR 解释器 -> Profile -> 热点 JIT`

而不是：

- `AST -> 直接 LLVM/MLIR 全量 JIT`
- 在 verifier、analysis、SSA 都没建立前就开始做热点编译

原因很简单：

- MATLAB-like 语言动态性强
- 当前项目仍在补语义覆盖和优化器基础设施
- JIT 的价值依赖稳定的 IR、稳定的 analysis 和稳定的 profile

## 自研 IR 的定位

当前自研 IR 仍应继续保留，并作为：

- 主语义承载层
- 主执行层
- fallback 层
- SSA 和优化器的输入层

这层 IR 已经具备：

- 显式 CFG
- `ValueId` / `ValueRef`
- `PhiInstruction`
- 多结果调用
- 源码位置信息

因此它不只是过渡产物，而是后续 JIT 的前端基础。

## MLIR 的定位

MLIR 值得考虑，但更适合作为热点后端，而不是当前项目的主 IR。

更合理的路线是：

`自研 IR -> 识别热点且稳定的片段 -> lower 到 MLIR/LLVM -> native code`

而不是：

`AST -> MLIR-first -> 替代现有 IR`

### 为什么当前不建议 MLIR-first

因为 MLIR 并不会自动解决下面这些问题：

- MATLAB-like 名字环境语义
- 多返回值调用
- 工作区和函数边界
- builtin/runtime 桥接
- fallback / deopt 边界

如果在这些基础设施尚未稳定前就全面转向 MLIR，会过早引入额外复杂度。

## 真正做 JIT 之前的前置条件

在当前仓库里，JIT 的前置条件应至少包括：

1. 稳定的 lowering
2. 稳定的解释器主链
3. `Verifier`
4. `PassManager`
5. CFG / dominator / def-use 等 analysis
6. `BuildPrunedSSA`
7. 基础优化 pass
8. 热点和类型/shape profile

没有这些基础设施，JIT 很容易变成“编译出一条快路径，但很难验证、很难回退、很难维护”。

## 第一批值得做的热点优化

相比“大而全”的 JIT，当前阶段更现实的优先级是：

### 1. 基础优化和解释器快路径

例如：

- 常量折叠
- DCE
- `SimplifyCFG`
- 少量稳定 builtin 的 fast-path

### 2. 纯数值表达式融合

对这类表达式：

- `A + B`
- `A + B .* C`
- `A + B .* sin(C)`

更现实的第一步通常不是完整 JIT，而是：

- 在 IR 或解释器中识别纯表达式子图
- 加 guard
- 命中时走 fused fast-path

这类工作既能带来早期收益，也能为后续热点 JIT 积累 profile 和 guard 经验。

### 3. 规则热点循环

当循环满足下列条件时，再考虑进一步专门化：

- 热度足够高
- trip count 稳定
- 类型/shape 稳定
- 没有高动态语义污染

对当前项目，这类循环仍应建立在统一的 CFG 和 `foreach_*` 协议之上，而不是再引入旧式专门循环节点。

## 当前不应优先投入的方向

下面这些方向不是没有价值，但都不应抢在基础设施之前：

- 全量 MLIR-first 重写
- 多面体优化
- 尾调用优化
- 大规模 deopt 框架
- 面向所有动态语义的一般性 JIT

这些能力都更适合在：

- verifier 稳定
- analysis 稳定
- SSA 稳定
- profile 稳定

之后再评估。

## 建议的实施顺序

如果把 JIT 路线压成当前项目的可执行顺序，更合理的是：

1. 继续扩大 lowering 和解释器覆盖面
2. 建立 `Verifier`
3. 建立 `PassManager`
4. 建立 CFG / dominator / def-use / SSA
5. 落地基础优化
6. 增加热点、类型和 shape profile
7. 做解释器内 fast-path / 融合原型
8. 最后再考虑 MLIR 热点后端

## 结论

对当前 Baltam_IR 来说，JIT 仍然值得做，但不是下一步。

下一步更重要的是把：

- IR
- 解释器
- verifier
- analysis
- SSA
- profile

这些基础设施补齐。

在那之后，再把 MLIR 或 LLVM 当作热点后端引入，会更稳、更工业，也更容易维护。
