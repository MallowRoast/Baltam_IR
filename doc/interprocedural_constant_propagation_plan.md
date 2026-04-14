# Baltam_IR 过程间常量传播规划

本文整理当前仓库如果要继续做“过程间常量传播”需要补哪些基础设施，以及推荐的落地顺序。

这里说的“过程间”主要指：

- 从 caller 把常量实参传播到 callee
- 在 callee 内推导返回值摘要
- 再把常量返回值传播回 caller

## 当前仓库现状

当前优化框架已经有一条稳定的函数内 pipeline：

- `ConstantFold`
- `DeadBranchElimination`
- `CopyPropagation`
- 第二轮 `ConstantFold`
- 第二轮 `DeadBranchElimination`
- 第二轮 `CopyPropagation`
- `DCE`
- `CFGSimplify`

但这条 pipeline 仍然是“逐函数独立”运行的：

- `optimize_module(...)` 只是遍历 `module.functions()`，依次对每个函数调用函数级 pipeline
- 当前 analysis manager 只有 `FunctionAnalysisManager`
- 当前 pass 抽象也只有 `FunctionPass`

这意味着：

- `call @f1(%a, %b)` 这类调用在当前优化阶段不会利用 callee 本身的 IR
- 即使 caller 传入的是常量，也不会把这些信息传到 callee
- 即使 callee 能推出常量返回值，也不会再把这个结果传回 caller

所以，过程间常量传播的第一前提不是“再补一个更强的 constant fold”，而是要补模块级优化框架。

## 过程间常量传播要解决什么问题

以 `test1` 里的：

```matlab
e = f1(a, b);
```

为例，当前函数内优化只能知道：

- `a = 1`
- `b = 2`

但由于 `f1` 是另外一个函数，caller 内看不到：

```matlab
z = x^2 + y^2;
```

因此当前只能保留：

```llvm
%e = call @f1(%a, %b)
```

如果做了过程间常量传播，理想结果是：

1. 在 caller 看到 `call @f1(1, 2)`
2. 把 `x = 1, y = 2` 作为常量环境传给 `f1`
3. 在 `f1` 内推出 `z = 5`
4. 把 `call @f1(...)` 的结果重写成常量 `5`
5. 再跑函数内 cleanup，把后续比较、分支、死代码继续折掉

## 必需的基础设施

### 1. 模块级 pass / analysis 框架

当前仓库只有函数级：

- `FunctionPass`
- `FunctionPassManager`
- `FunctionAnalysisManager`

如果要做过程间优化，至少还需要：

- `ModulePass`
- `ModulePassManager`
- `ModuleAnalysisManager`

原因很直接：

- 过程间优化的输入不再是“单个函数”
- 它要同时看 caller、callee 和整个模块里的函数关系
- 只靠函数级 analysis 无法稳定缓存调用图、函数摘要、SCC 信息

### 2. 调用图和 direct callee 解析

当前 `SSACallNode` 里只有：

- direct call: `direct_symbol`
- indirect call: `indirect_value`

过程间传播至少要回答：

- 某个 `direct_symbol` 是否对应当前 `Module` 里的一个 `Function`
- 这个 `Function` 是 local function、primary function，还是 builtin / internal helper

所以需要一层调用图分析，最小输出至少包括：

- 每个函数的 direct outgoing calls
- 每个 direct call 对应的 `Function*`，或者“不是模块内函数”

没有这个分析，就没法把 `call @f1(...)` 和真正的 `Function f1` 关联起来。

### 3. 常量传播 lattice

过程间传播不能只用“是不是 `SSANumberNode`”这种局部判断，它需要更稳定的摘要表示。

最小建议用三态 lattice：

- `Unknown`
- `Constant(value)`
- `Overdefined`

其中：

- `Unknown` 表示还没算出来
- `Constant(value)` 表示当前已知常量
- `Overdefined` 表示不是单一常量，或者保守起见不能再折

这套 lattice 至少要用于：

- 形参摘要
- 返回值摘要
- 必要时也可扩展到函数内 SSA 值摘要

### 4. 函数摘要

过程间传播不能每遇到一个调用点就完整“模拟执行一次 callee”，不然会非常难控。

更实用的做法是为每个函数维护摘要，例如：

- 输入参数摘要向量
- 返回值摘要向量
- 该函数是否有未知副作用

对当前仓库，v1 至少应支持：

- 固定输入个数
- 固定输出个数
- 返回值全都是 SSA 结果

第一版不建议一开始就支持：

- `varargin`
- `varargout`
- 间接调用
- 动态输出个数依赖 `nargout`

### 5. SCC / worklist 求解

过程间传播天然会遇到：

- 递归
- 互递归
- 摘要反复收敛

因此不能靠“按源码顺序跑一遍”来做。

至少需要：

- worklist
- 在调用图上的 SCC 视角
- 摘要变化时重新入队相关 caller / callee

就算 v1 不支持递归，也建议一开始就把 worklist 结构搭出来；否则后面扩展会推倒重来。

### 6. 副作用和可传播边界

不是所有 call 都适合做过程间常量传播。

必须先明确哪些调用可进过程间传播，哪些必须保守处理。

当前仓库里至少要把这些情况先排除或单独建模：

- builtin call
- internal helper
- indirect call
- `disp`
- `error`
- `global load/store`
- `nargin`
- `nargout`
- 与 workspace / runtime object 状态相关的 helper

第一版最稳妥的策略是：

- 只对“模块内 direct call 且目标是普通函数”做过程间传播
- 其他 call 一律视为 `Overdefined`

### 7. 调用点重写后的函数内 cleanup

过程间常量传播本身通常只负责：

- 发现常量实参
- 发现常量返回值
- 把 call result 改写成常量

但真正把 IR 清干净，还需要再接函数内优化：

- `ConstantFold`
- `DeadBranchElimination`
- `CopyPropagation`
- `DCE`
- `CFGSimplify`

否则 caller 里虽然 call result 已经成了常量，后续链条仍然会残留。

## 建议的最小可用版本

### v1 目标

先只支持：

- 同一 `Module` 内的 direct function call
- callee 能唯一解析到本模块 `Function`
- 无递归
- 固定输入 / 输出个数
- 无 `varargin` / `varargout`
- 无间接调用
- 无 builtin / internal helper 参与过程间求值
- 无 global / workspace 副作用

这个范围已经足够覆盖很多脚本内 local function 场景，例如 `test1` 里的 `f1(a, b)`。

### v1 结果形式

v1 不必一开始就做函数克隆，先做“返回值摘要传播”即可：

1. 在 caller 收集 call site 的常量实参摘要
2. 把这些摘要传给 callee
3. 在 callee 内利用现有常量折叠能力推导返回值摘要
4. 若某个返回值是 `Constant(value)`，则把 caller 里的对应 `call result` 直接重写成 `SSANumberNode`
5. 对 caller 再跑一轮现有函数内 cleanup

这已经能拿到第一批收益，而且不会太早把系统复杂度拉爆。

## 为什么 v1 不建议一开始就做函数特化

函数特化当然是过程间传播的强力版本，但它不是第一步。

例如：

- 调用点 A: `f(1, 2)`
- 调用点 B: `f(x, y)`

如果不做特化，`f` 的整体摘要可能会因为调用点 B 退化成 `Overdefined`。

如果做了特化，就可以生成：

- `f$const_1_2`
- 原始 `f`

然后把调用点 A 改成调特化版本。

但函数特化会立刻带来更多问题：

- 如何命名和缓存特化函数
- 如何避免重复克隆
- 如何处理递归特化
- 如何维护 `Module` 里的函数集合和调用图
- 如何控制 code size

所以建议顺序是：

- 先做“无特化的过程间摘要传播”
- 再做“按常量实参向量进行函数特化”

## 推荐落地顺序

### Step 1: 补模块级优化框架

新增：

- `ModulePass`
- `ModulePassManager`
- `ModuleAnalysisManager`

第一版只要能跑：

- module verifier
- module analysis cache
- module pass pipeline

### Step 2: 补调用图分析

最小输出：

- `Function* -> 调用点列表`
- `SSACallNode* -> 目标 Function* 或 null`

只覆盖模块内 direct call 即可。

### Step 3: 补函数摘要结构

建议定义：

- 参数摘要向量
- 返回摘要向量
- 是否含未知副作用

先只支持三态 lattice：

- `Unknown`
- `Constant`
- `Overdefined`

### Step 4: 做 v1 过程间常量传播 pass

v1 pass 的职责建议只包括：

- 收集调用点常量实参
- 更新 callee 参数摘要
- 反复求解返回摘要
- 把 caller 里的 `call result` 改写成常量

先不要在这个 pass 里顺手做太多 cleanup。

### Step 5: 在过程间 pass 后复用现有函数内 pipeline

建议顺序：

1. 过程间常量传播
2. 对受影响函数重新跑当前函数内优化 pipeline

这样可以直接吃到现有 `ConstantFold / DBE / CopyPropagation / DCE / CFGSimplify` 的收益。

### Step 6: 再考虑函数特化

等 v1 稳定后，再补：

- 基于常量实参的函数克隆
- call site 改写到特化函数
- 特化缓存和去重

## 对当前仓库的直接建议

如果下一步真的要开始做，我建议先不要上来追求“完整 SCCP + 过程间 + 特化”。

更稳的起点是：

1. 先补 `ModulePass` / `ModuleAnalysisManager`
2. 先补 `CallGraphAnalysis`
3. 先做一个 `InterproceduralConstantPropagationPass v1`

v1 只支持：

- 模块内 direct local function
- 无递归
- 固定参数和返回值
- 无可见副作用

这样已经足够先把：

```llvm
%e = call @f1(%a, %b)
```

在 `a`、`b` 为常量时传播成：

```llvm
%e = const 5
```

然后再把 caller 里后续分支和比较继续折掉。

## 后续扩展方向

v1 落地后，再逐步扩展到：

- 递归 / 互递归
- 函数特化
- builtin purity 建模
- internal helper purity 建模
- `varargin` / `varargout`
- `nargin` / `nargout`
- 基于 SCCP 的更强过程间 lattice 求解

到这一步时，过程间常量传播就不只是“调用点替换常量”了，而会逐步演变成更一般的模块级数据流优化框架。
