# Baltam_IR JIT 方案整理

## 目标

本文档整理当前项目的 JIT 演进路线，并回答两个关键问题：

- 当前自研 IR 路线是否还应该继续
- MLIR 是否适合作为后续 JIT 方案

同时补充说明两类常被提到的优化技术在 MATLAB-like 语言中的适用性：

- 多面体优化
- 表达式模板 / 融合计算

## 当前判断

当前项目最合适的主线仍然是：

`AST -> 自研 CFG IR -> IR 解释器 -> Profile -> 热点 JIT`

而不是：

- `AST -> 直接 LLVM/MLIR 全量 JIT`
- `AST -> 长期停留在递归解释器`

原因是当前最重要的任务仍然是把 MATLAB-like 语言的动态语义稳定落到一个可执行、可分析、可回退的载体上。

现有自研 IR 已经具备这些优势：

- 显式 `BasicBlock + terminator + CFG`
- 能承载源码位置信息
- 更贴近当前 lowering 和解释器实现
- 适合做 profile、热点识别和 fallback

因此，这层 IR 仍应作为主执行 IR 保留。

## MLIR 的定位

MLIR 可以考虑引入，但更适合作为热点优化/JIT 后端，而不是当前阶段的主 IR。

推荐定位：

- 当前自研 IR：语义承载层、主执行层、fallback 层
- MLIR：热点片段的优化和代码生成层

更合理的路线是：

`AST -> 自研 IR -> IR 解释器 + Profile -> 热点片段 lower 到 MLIR -> LLVM/native code`

而不是直接改成：

`AST -> MLIR -> 解释/JIT -> 替代现有 IR`

### 为什么不建议现在 MLIR-first

因为 MLIR 并不会自动解决下面这些语言问题：

- MATLAB-like workspace 语义
- 名字查找和动态绑定
- 多返回值调用
- builtin/runtime 桥接
- 脚本与函数文件差异
- deopt / fallback 语义边界

如果现在全面转向 MLIR，会过早引入额外复杂度：

- 设计 dialect
- 设计类型系统
- 设计 boxed/unboxed 边界
- 设计 runtime helper 调用规范
- 设计 deopt 状态恢复机制

这些工作在当前阶段投入较大，但收益未必立刻出现。

## 推荐分层

### 1. 当前自研 IR 层

职责：

- 表达源码结构 lower 后的显式控制流
- 作为解释器的主执行载体
- 提供源码映射、调试、验证和 fallback

这一层建议继续保持较高层但可执行：

- `Name`
- `Number`
- `BinOp`
- `Call`
- `Assign`
- `CondJump`
- `Jump`
- `Return`

后续再逐步增加：

- `UnaryOp`
- `While`
- `For` lower 后的 loop CFG
- 多返回值函数调用
- 局部函数调用

这里有一个重要约束：

- `for` 不应长期保留成 `ForRangeInstruction`
- `for i = 2:4` 不应被视为特殊循环种类
- `node_colon` 应先 lower 成 `colon(...)`
- `for` 本身应统一 lower 成 `foreach_init / foreach_iterate` 的普通 CFG

也就是说：

```matlab
for i = a
for i = 2:4
```

这两种形式在 IR/JIT 入口层都应统一成 `for i = expr`。

### 2. Profile 层

职责：

- 记录函数热度
- 记录回边计数
- 记录调用点热度
- 记录粗粒度类型和 shape 信息

建议先记录：

- 值类别：scalar / matrix / complex / object
- 标量基础类型：bool / int / double
- 是否恒定标量 shape
- builtin 调用目标是否稳定

### 3. MLIR JIT 层

MLIR 只处理热点、稳定、低风险片段。

第一阶段适合 lower 到 MLIR 的内容：

- 标量算术
- 标量比较
- 简单 `if`
- 简单 `while/for`
- 稳定 builtin：`sin/cos/exp/plus/times`

第一阶段不适合 lower 到 MLIR 的内容：

- `eval`
- 高动态名字解析
- 不稳定函数句柄
- `classdef`
- 复杂 `cell/struct`
- 频繁变 shape 的数组计算

## 如果要引入 MLIR，建议的形状

推荐不是直接对 AST 建模，而是先从当前 IR lower 到一个较薄的 MLIR dialect。

可考虑两层：

### `ba_hir`

较贴近当前 IR，保留：

- basic block
- branch / jump / return
- load/store local
- call builtin
- boxed value
- runtime helper call

这层的目标不是极致优化，而是稳定承接当前 IR。

### `ba_num`

更低层、面向热点专门化：

- `f64`
- `i1`
- 简单标量算术
- guards
- 类型/shape 假设

这层更适合再降到 LLVM。

## 多面体优化对 MATLAB-like 语言的价值

有价值，但作用范围比很多人想象得窄。

多面体优化最擅长的是：

- 仿射循环嵌套
- 静态可分析的数组下标
- 循环变换：tiling、fusion、interchange、parallelization

它对下面这类代码收益可能很大：

```matlab
for i = 1:n
    for j = 1:m
        A(i, j) = B(i, j) + C(i, j);
    end
end
```

或者更规则的 stencil / 矩阵遍历。

但对 MATLAB-like 语言整体而言，限制也很明显：

- 很多值的 shape 在运行时才稳定
- 下标不一定是简单仿射表达式
- 函数调用和 builtin 边界很多
- 脚本代码经常混合标量、矩阵、对象和控制流

所以结论是：

- 多面体优化不是主线基础设施
- 它更像后续“规则循环 hotspot”的专项优化能力
- 前提是已经有稳定的 loop IR、shape 信息和 alias/side-effect 判断

对于当前项目阶段，它不是第一优先级。

## 表达式模板对 MATLAB-like 语言的价值

表达式模板这类技术，对 `A + B * sin(C)` 这类表达式是有启发的，但不能直接照搬成整体执行架构。

它擅长解决的问题是：

- 避免中间临时对象
- 把复合逐元素表达式融合成一次遍历
- 将 `A + B * sin(C)` 变成单 kernel 扫描

如果 `A`、`B`、`C` 都是 shape 一致的 dense array，那么潜在收益很明显：

- 少一次或多次临时分配
- 少几轮数组遍历
- cache 友好

### 但它和 JIT 不是一回事

表达式模板通常依赖：

- 编译期类型
- 编译期组合表达式结构

而 MATLAB-like 语言的问题在于：

- 值类型和 shape 常常运行时才知道
- 名字绑定和 builtin 分派也可能动态
- 同一语句既可能是标量路径，也可能是矩阵路径

所以在我们这里，更合理的对应物不是“直接用 C++ 表达式模板”，而是：

- 在 IR 中识别可融合的纯表达式子图
- 在运行时确认类型/shape 稳定
- 再选择：
  - 解释器中的专门 fused fast-path
  - 或 lower 到 MLIR/LLVM 生成 fused kernel

## 对 `A + B * sin(C)` 这类表达式更现实的优化手段

在 MATLAB-like 场景里，更推荐按收益顺序考虑这些手段。

### 1. 运行时 fast-path

对常见稳定组合提供专门路径：

- dense double matrix
- scalar double
- logical matrix

例如识别：

`plus(A, times(B, sin(C)))`

## `for` 相关优化

`for` 的优化不应建立在语法特判上，而应建立在统一的 `for i = expr` 模型上。对当前项目，先记两类明确的优化方向。

### 1. `colon` 不直接求值，而是返回 `Range` 对象

当前更直接的做法是：

```text
node_colon -> call "colon"(args...)
```

后续可进一步优化为：

- `colon` 不立即物化完整数组
- 而是返回一个轻量 `Range` 运行时对象
- `foreach_init` 和 `foreach_iterate` 直接消费这个 `Range`

这样做的好处：

- 避免 `2:1000000` 先构造整段临时数组
- 更贴近 MATLAB-like 里的范围语义
- 后续更容易做 loop trip-count 分析
- 对解释器和 JIT 都有帮助

这也是把 `node_colon` 视为普通表达式，但不急着把它完全物化的关键优化点。

### 2. 对简单范围循环退化成普通 C++ `for`

当满足下面条件时：

- `for` 的右侧是 `2:4` 或 `1:2:9` 这类稳定的 `colon`
- lowering / profile 能确认 trip count 稳定
- 循环变量在循环体中没有被重新赋值
- 循环体里没有破坏该假设的动态行为

就可以在热点 JIT 中把它专门化成更接近原生代码的形式：

```cpp
for (auto i = begin; i <= end; i += step) {
    ...
}
```

这类专门化的价值在于：

- 避免 `foreach_iterate` 的通用调度开销
- 便于 LLVM/MLIR 做常规循环优化
- 更容易获得 trip-count、回边、归纳变量等信息

但这个优化需要保守触发。只要循环变量可能被循环体改写，或者右侧 `expr` 不是稳定的 `colon/Range`，就应退回通用的 `foreach_init / foreach_iterate` 路径。

如果：

- 都是 dense double
- shape 匹配
- 无别名风险

则走 fused kernel。

这是最现实的第一步。

### 2. 表达式融合

在 IR 或 builtin 层识别纯元素级表达式 DAG，并融合成单次遍历。

这对下面这类表达式很有价值：

- `A + B`
- `A + B * C`
- `A + B .* sin(C)`
- `alpha * X + Y`

这类优化对数组语言非常重要，优先级通常高于多面体优化。

### 3. 常量传播和 copy elimination

对于标量和小对象，先做：

- 常量折叠
- 公共子表达式消除
- 无用赋值删除
- 避免不必要 boxing/unboxing

这些实现成本低，早期收益很稳定。

### 4. BLAS / 库调用识别

某些模式不需要自己 JIT，直接识别后调用成熟库更好：

- `gemm`
- `axpy`
- `dot`
- `norm`

这往往比自己编译通用循环更现实。

### 5. 热点 JIT

当某段表达式或循环足够热，并且类型/shape 稳定时，再 lower 到 MLIR/LLVM 做专门化。

## 表达式融合原型设计

为了让 `A + B * sin(C)` 这类表达式更高效，当前阶段可以先设计一版不依赖 MLIR 的最小原型。

目标不是一开始支持所有数组表达式，而是先支持：

- 纯表达式
- 无副作用
- 逐元素语义明确
- 输入输出 shape 易于检查

### 原型适合覆盖的表达式

第一批建议只覆盖：

- `A + B`
- `A - B`
- `A .* B`
- `A + B .* C`
- `A + B .* sin(C)`
- `alpha * X + Y`

这些表达式的共同特征是：

- 结果值只依赖输入元素
- 不需要复杂控制流
- 可以自然 lower 成一次遍历

### 原型不建议先支持的内容

第一版先不要碰：

- 矩阵乘法
- 广播语义复杂的表达式
- 下标读写混合的表达式
- 用户自定义函数调用
- 可能触发副作用或异常语义差异的 builtin
- shape 或类型高度不稳定的表达式

### 建议的识别时机

可以有两种落点：

1. 在 lowering 后的 IR 上识别表达式 DAG
2. 在解释器执行某条 `AssignInstruction` 前，向上看其右值子图

更推荐先从 IR 上识别，因为：

- 当前 IR 已经脱离 AST 语法噪声
- 更容易限制“只处理纯表达式子图”
- 后面也更容易把识别结果复用给 MLIR lower

### 可融合子图的判定条件

一个表达式子图若满足下面条件，可考虑融合：

- 根是单个赋值或单个返回值计算
- 子图只包含纯数值节点
- 不包含控制流
- 不包含可能修改工作区的调用
- 所有中间值仅在当前子图内部使用
- 输入值读取后直到表达式结束不被覆盖

第一版可把候选节点限制为：

- `Name`
- `Number`
- `BinOp`
- 少量纯 builtin `Call`

### 原型运行时 guard

即使识别出可融合子图，也仍然需要运行时 guard。

第一版 guard 可以只检查：

- 所有输入都是 dense double scalar 或 dense double matrix
- 所有输入 shape 相容
- 输出与输入没有危险别名
- builtin 调用目标是稳定且可融合的内建函数

如果 guard 失败，直接回退到普通 IR 解释路径。

### 可能的 IR 承载方式

有两种可选做法：

#### 方案 A：不改 IR，解释器内临时识别

优点：

- 改动小
- 便于快速验证收益

缺点：

- 可视化和调试较弱
- 后续复用给 JIT 没那么自然

#### 方案 B：增加显式 `FusedExpr` 节点

例如让 IR 中出现一条“融合表达式”节点，内部持有小型表达式树或 DAG。

优点：

- IR 中能显式表达优化结果
- 更利于 profile 和后续 lower 到 MLIR

缺点：

- 需要扩展 IR 结构
- 第一版工程量稍大

对于当前阶段，更建议：

- 先做方案 A 验证思路
- 如果收益明显，再考虑引入显式 `FusedExpr`

### 解释器中的执行形态

解释器侧第一版可以提供一个专门入口：

```cpp
bool try_execute_fused_expression(const AssignInstruction& inst, Frame& frame);
```

语义是：

- 尝试识别 `inst.value()` 对应的纯表达式子图
- 若满足 guard，则直接执行 fused fast-path，并返回 `true`
- 若不满足，则返回 `false`，继续走普通解释执行

这样不会破坏当前解释器主流程，也便于逐步扩大支持面。

### 与 MLIR 的关系

表达式融合原型不应被看作 MLIR 的替代品，而应被看作：

- 当前 IR 层的局部加速手段
- 为以后热点 JIT 提供识别和 guard 经验

后续如果引入 MLIR，可以把“已识别的可融合纯表达式”作为很自然的 lower 单元。

### 原型阶段建议记录的 profile

为了以后决定哪些表达式值得继续优化，建议对融合候选补一些轻量统计：

- 命中次数
- guard 失败次数
- 失败原因
- 输入类型模式
- 输入 shape 模式

这样可以帮助判断：

- 是不是表达式本身就很热
- 是不是 shape 不稳定导致融合收益有限
- 是否值得为某些常见模式补专门 fast-path

### 推荐实现顺序

表达式融合原型建议按下面顺序做：

1. 定义“可融合纯表达式”的判定规则
2. 先只支持 dense double scalar/matrix
3. 在解释器里实现 `try_execute_fused_expression`
4. 对 `A + B .* sin(C)` 这类模式补一个最小 fast-path
5. 增加命中/失败 profile
6. 再决定是否引入显式 `FusedExpr` IR 节点

## 尾调用优化

尾调用优化可以纳入长期执行方案，但不建议作为当前阶段的优先事项。

### 适用价值

尾调用优化主要解决的是：

- 深层递归导致的栈帧增长
- `call + return` 模式下不必要的 frame 分配

它对某些递归型数值程序和解释器辅助逻辑有价值，但对 MATLAB-like 语言的整体性能收益通常不如：

- 数组表达式融合
- 循环热点优化
- builtin fast-path

因此更合理的定位是：

- 可支持
- 不必优先
- 应在调用调度模型稳定后再做

### 和当前 Frame 设计的关系

当前 `Frame` 设计不需要为了尾调用优化专门改造。

原因是尾调用优化的核心问题不在于 `Frame` 如何存数据，而在于：

- 函数调用是否采用递归执行模型
- 尾位置调用能否被解释器识别
- 当前执行上下文能否被复用或替换

因此，当前的 `Frame` 仍然可以保持：

- `Function*`
- `Frame* caller`
- 符号表
- `nargin/nargout`
- `returned`

后续真正做尾调用优化时，再调整调用调度器即可。

### 推荐实现方向

更推荐的方式不是在 `Frame` 中塞大量尾调用状态，而是把函数调用执行模型改成迭代式调度。

可以考虑两条路线：

#### 1. 复用当前 frame

当识别到：

- 当前函数最后一步是调用另一个函数
- 调用结果直接作为当前函数返回

则不再新建新的 C++ 调用层，而是重置当前执行上下文，切换到被调函数。

#### 2. Trampoline / 调度循环

让解释器的函数执行不直接递归进入下一级 `execute_function`，而是返回一个“下一次调用请求”，由外层循环继续调度。

这条路线通常更稳，因为：

- 更容易消除 C++ 层递归
- 更容易和 fallback / deopt 协同
- 更适合后续接入 profile 和热点 JIT

### 对当前 IR 的要求

要支持尾调用优化，通常需要：

- 能识别尾位置调用
- 调用和返回语义清晰
- 多返回值绑定规则稳定
- caller / callee 的输出约定明确

当前这套 IR 已经有一定基础：

- 显式 CFG
- `ReturnInstruction`
- `Function` 上的输入输出参数名

但在真正做尾调用优化前，仍建议先补齐：

- 更完整的函数调用支持
- 多返回值调用与返回语义
- 局部函数 / 普通函数调用路径

### 当前阶段建议

当前更推荐的顺序是：

1. 继续扩大 AST -> IR lowering 覆盖
2. 补齐解释器中的函数调用语义
3. 做 profile、表达式融合和热点路径优化
4. 再评估尾调用优化是否值得投入

因此，尾调用优化可以记入路线图，但不应抢在表达式融合、循环热点和 MLIR JIT 之前。

## 当前阶段的建议优先级

对于当前项目，更建议按下面顺序推进：

1. 扩大 AST -> IR lowering 覆盖
2. 补齐 IR 解释器语义
3. 增加 profile：hotness、type、shape
4. 增加简单常量折叠和局部代数化简
5. 设计“纯数值表达式融合”原型
6. 再考虑 MLIR 热点 JIT
7. 最后再评估是否要做规则循环的多面体优化

## 结论

结论可以概括为三句：

1. 当前自研 IR 路线应该继续，它是主执行和 fallback 载体。
2. MLIR 值得采用，但更适合做热点 JIT 后端，而不是当前主 IR。
3. 对 MATLAB-like 语言而言，表达式融合通常比多面体优化更早产生收益；多面体优化更适合后续规则循环热点。
