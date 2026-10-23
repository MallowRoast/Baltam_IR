# Matlab 语言解释器总体设计

## 总体方案

```text
M 函数 source code → AST → non-SSA IR → based SSA IR → optimized SSA IR → JIT
```

这是代码的构建过程。运行时希望大部分代码执行 SSA IR；non-SSA IR 是完整的 M 语言执行基线，也是在 SSA 无法继续执行时的回退目标。JIT 是后续执行层，本文不展开它的具体设计。

## 各阶段做什么

| 阶段 | 为什么做 | 大概怎么做 | 主要问题 |
| --- | --- | --- | --- |
| source code → AST | 取得 M 源码的语法结构 | 直接复用 kernel 已实现的解析 | `node_name` 的静态分类，以及 AST 与 `pcdata` 的关系 |
| AST → non-SSA IR | 建立所有 M 语法都能执行的基线 | 引入 slot 表示变量，并把 AST 转成可执行的 non-SSA IR | slot 与运行时动态变量如何配合，确保语义完整 |
| non-SSA IR → based SSA IR | 让大部分普通代码进入 SSA | 先按极少发生的 `eval`、`evalin`、`assignin` 不影响 SSA 变量的情况构建；实际遇到时回退 | 回退时如何恢复 slot 状态，以及如何对应回 non-SSA 的执行位置 |
| based SSA IR → optimized SSA IR | 在 SSA 上执行常量折叠等优化 | 先按运行时 path 不改变优化结果的假设执行 pass | 如何记录优化依赖的函数，并在执行前检查用户自定义函数 |
| optimized SSA IR → JIT | 将优化后的 IR 用于后续机器码执行 | 沿用 SSA 的优化结果和运行时回退思路 | JIT 也需要能够回退；具体机制留待 JIT 设计 |

## 关键问题展开

### 1. AST 与 non-SSA 的基础

源码到 AST 已有 kernel 实现。这里建议让 `node_name` 增加静态类型枚举，例如变量、函数、枚举，使后续 IR 构建能够直接使用已知的分类。另一个目标是 AST 不再持有 `pcdata`。由于现有 AST 解释器还会长期存在，这项调整需要与旧执行路径并行推进。

AST 到 non-SSA 的关键是引入 slot。源码中能确定的变量有对应 slot；运行时按名字产生的变量仍需要由动态 workspace 处理。无论某种 M 语法以后能否 SSA 化，non-SSA 都必须能执行它，这样 SSA 回退后才有可靠的地方继续。

### 2. SSA 到 non-SSA 的运行时 Deopt

`eval`、`evalin`、`assignin` 可能在运行时按名字访问或修改变量。如果为了这些少见情况放弃 SSA，大部分普通代码也失去了使用 SSA 的机会。因此 based SSA 先按这些动态操作不会改变 SSA 中变量当前值的常见情况构建；当实际运行到会破坏这个假设的操作时，再回到 non-SSA。

回退需要解决两件事：

1. **恢复状态：** 对每个 slot，找出当前应有的值和绑定状态。SSA 执行期间变量可能只保存在 SSA 值中，因此需要知道此刻哪个 SSA 值对应哪个 slot。
2. **恢复位置：** 知道当前 SSA 执行位置对应 non-SSA 的哪条指令，以及应从这条指令之前还是之后继续。已经执行过的操作不能再执行一次。

例如当前函数遇到 `eval`，应在执行 `eval` 前回退，让 non-SSA 执行这次调用。若被调用函数通过 `assignin('caller', ...)` 改写了 caller 的已有 slot，caller 则在调用结束时回退，从这次调用之后继续。恢复时要保留刚写入的值，不能被旧的 SSA 值覆盖。只读取 caller 的 `evalin` 也必须能读到 caller 当前的 SSA 值。

为了避免回退信息过大，不必在每条 SSA 指令处复制全部 slot 值。可以记录 slot 与 SSA 值映射的变化，并设置适当的检查点；只有真正回退时才重建完整状态。分支、循环以及优化 pass 改写指令后，映射如何保持正确，是这部分仍需重点设计的内容。

### 3. optimized SSA 的函数依赖

从 based SSA 构建 optimized SSA 时，常量折叠等优化 pass 可能依赖一个结论：**用户没有自定义对应的函数**。只有这个结论成立，pass 才能按预期的函数行为计算结果，并把原来的运算替换掉。

这个结论在优化时成立，不代表运行时仍然成立，因为用户可以改变 path。若不记录依赖，执行 optimized SSA 前就不知道该检查哪些函数。因此，pass 每次使用这一结论时，都要记录所依赖的函数。解释执行 optimized SSA 前，逐一检查当前 path 下用户是否自定义了这些函数；只要相关结论不再成立，就不能使用这份 optimized SSA。

### 4. 三种 IR 的失效

non-SSA、based SSA 和 optimized SSA 都来自同一份 M 函数源码。为源文件记录时间戳，并为其缓存版本建立版本号；再次使用时按需检查源文件是否变化。源文件变化后，让该文件对应的三种 IR 一起失效，再从新源码构建。这样无需每次执行都重新生成 IR。

源码失效与上一节的 path 依赖检查是两件事：前者决定三种 IR 是否仍对应当前源码，后者决定 optimized SSA 中基于函数分派的优化是否仍成立。
