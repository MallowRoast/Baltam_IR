# Matlab 解释器 IR 设计演进记录

## 背景

本文记录 Matlab 语言解释器在中间表示（IR）设计上的几次主要思路变化，重点说明每个方案的动机、收益、问题，以及当前更倾向的方向。

设计目标并不只是“能跑”，而是同时兼顾以下几点：

- 解释执行的语义完整性
- 热点路径的优化与 JIT 编译能力
- 动态语言特性的可建模性
- IR 结构的长期可维护性

## 方案一

### 流程

`AST -> non-SSA IR -> interpret/profile -> JIT -> SSA -> LLVM IR`

### 设计动机

最初的思路是先从 AST 降到一个接近执行语义的 non-SSA IR，用它支撑解释执行、profile 和前期实现；等真正进入 JIT 阶段时，再构造 SSA 并继续生成 LLVM IR。

### 问题

这个方案的主要问题是，non-SSA IR 在优化上的能力较弱。它更适合作为执行层 IR，但不适合作为强分析和强优化的载体。

尤其是在希望尽早做一些通用编译优化时，non-SSA IR 的局限比较明显，例如：

- 数据流信息不够直接
- 复制传播、常量传播等优化不够自然
- 死代码删除和控制流简化实现成本更高
- 很难形成一套干净、统一的优化框架

因此，方案一更像是“先执行，后优化”，但在工程上会导致早期 IR 的分析价值偏低。

## 方案二

### 流程

`AST -> non-SSA -> untyped SSA -> optimized untyped SSA -> interpret/profile -> JIT -> typed SSA -> LLVM IR`

### 设计动机

由于 non-SSA IR 的优化能力有限，下一步思路是尽早引入 untyped SSA，把它作为主要的分析 IR 和优化 IR。这样可以在类型信息尚未充分稳定之前，就先利用 SSA 的结构优势做一批基础优化。

### 收益

这个方案在实验上取得了明显效果。基于 untyped SSA，已经可以较自然地实现一系列基础优化 pass，例如：

- 常量折叠
- 死分支消除
- 死代码消除
- 控制流简化
- 复制传播

这些优化的效果很好，也说明 SSA 作为分析和优化载体是有效的。

### 新问题

虽然优化收益明显，但方案二仍保留了 non-SSA 阶段，而这个阶段的存在价值开始变得可疑。既然主要分析和优化工作都转移到了 untyped SSA，那么 non-SSA 更像是一个过渡层，而不是一个真正有独立价值的核心 IR。

这引出了两个问题：

- 是否还有必要长期保留 non-SSA 阶段
- 是否应该把分析 IR 与执行 IR 分离

## 方案三

### 流程

`AST -> untyped SSA -> optimized untyped SSA -> bytecode -> interpret/profile -> JIT -> typed SSA -> LLVM IR`

### 设计动机

在方案二基础上，进一步的想法是去掉 non-SSA，把 untyped SSA 明确为分析 IR；同时为了持久化和执行稳定性，引入 bytecode 作为执行 IR，从而把“分析优化”和“实际执行”分开。

这个方案的核心判断是：

- SSA 更适合分析和优化
- bytecode 更适合解释执行和 profile
- 分析 IR 与执行 IR 不应该完全混在一起

### 优点

相比方案二，方案三的结构更加清晰：

- untyped SSA 负责高层分析与优化
- bytecode 负责解释执行与 profile
- typed SSA 负责热点路径的进一步编译

从编译器结构上看，这是一种更分层的设计。

### 问题

在真正面对 Matlab 的动态特性时，方案三暴露了更深层的问题。SSA 在处理下列动态行为时非常不自然：

- `eval`
- `clear`
- `cd`
- `addpath`
- `mex`

这些特性会破坏 SSA 最依赖的一些前提：

- 名字绑定稳定
- 控制流边界清晰
- 环境与工作区状态可局部分析
- 调用目标和内存效果可收敛

为了让 untyped SSA 继续承载这些语义，不得不引入大量内存屏障、环境失效点或保守的 side effect 建模。这样虽然形式上还能维持 SSA，但会直接削弱 SSA 优化机制的有效性。

换句话说，方案三的问题不是“SSA 不好”，而是“SSA 不适合作为 Matlab 完整动态语义的主承载 IR”。

## 方案四

### 流程

`AST -> HIR (类似 non-SSA) -> bytecode -> interpret/profile -> JIT -> typed SSA -> LLVM IR`

### 设计动机

基于方案三暴露的问题，新的思路是不再让 untyped SSA 处于主干位置，而是重新引入一个更偏语义层的 HIR，负责承载 Matlab 的动态语义；再由 HIR 降到 bytecode，用于真实解释执行和 profile；最后只在 JIT 阶段按需构造 typed SSA。

### 核心判断

方案四并不是简单地“回到 non-SSA”，而是将 HIR 重新定义为语义 IR，而不是早期那个优化能力较弱的过渡 IR。

这里的关键区别在于：

- HIR 的职责是表达语义，不是强行承担所有优化
- bytecode 的职责是稳定执行，不是承担高层分析
- SSA 的职责是热点优化，不是全局语义建模

### 方案四相比前三个方案的优势

- 避免让 SSA 直接承载 Matlab 的全部动态特性
- 避免大量 barrier 污染全局 IR
- 更容易把解释器语义和 JIT 优化分层
- 更符合动态语言“先执行、后特化”的实现路径

## 当前更倾向的总体结构

如果进一步收敛，当前更合理的结构不是“全程序长期保留 untyped SSA”，而是：

`AST -> semantic HIR -> bytecode -> interpreter/profile -> hot region or function -> typed SSA -> LLVM IR`

这里有几个重要原则。

### 1. 让执行 IR 和优化 IR 分离

bytecode 和 interpreter 应该是语义执行主线。它们负责：

- 完整、稳定地执行 Matlab 语义
- 收集 profile
- 作为动态行为的真实观察点

SSA 不应承担这条主线，而应该只服务于热点编译。

### 2. HIR 必须是语义层 IR

HIR 的价值不在于“低级”，而在于显式表达那些无法自然 SSA 化的动态语义，例如：

- workspace 或 symbol table 访问
- 动态名字解析
- 动态函数分发
- 路径和环境修改
- 外部不透明副作用

如果这些语义被隐藏在普通变量读写里，后续无论是 bytecode 生成还是 JIT 特化都会很混乱。

### 3. SSA 采用按需构造，而不是全局常驻

对于数值计算密集、控制流相对稳定、类型趋于收敛的热点代码，可以在 JIT 阶段从 bytecode 或 region graph 构造 SSA，并插入必要的 guard。

对于包含强动态行为的路径，则应：

- 保持在 HIR 或 bytecode 层执行
- 或在进入动态操作前退出优化路径
- 或把这些点作为 deopt / invalidation 边界

这比在全局 SSA 上堆叠大量 barrier 更健康。

### 4. 将优化限定在“SSA region”中

可以把整个系统理解为一种“SSA region”模型：

- 普通数值热点代码进入 SSA，进行强优化
- 遇到 `eval`、`clear`、`addpath`、`mex` 等强动态操作时退出 SSA
- 当重新进入稳定区域时，再次构造 SSA

这种模型更符合 Matlab 这类动态语言的实际行为特征。

## 对四个方案的总结判断

### 方案一

优点是实现路径直接，执行导向明显。缺点是 non-SSA IR 的优化价值不高，前期 IR 主要只是过渡层。

### 方案二

优点是尽早引入 SSA，优化收益显著。缺点是仍保留了价值有限的 non-SSA 过渡阶段，结构上开始出现重复。

### 方案三

优点是分析 IR 与执行 IR 分离得更清晰。缺点是把 untyped SSA 放在主语义链路上，最终会被 Matlab 的动态语义反噬。

### 方案四

当前看是更合理的方向，但前提是 HIR 必须被设计成真正的语义 IR，而不是旧 non-SSA IR 的简单回归。

## 当前结论

当前更推荐的方向是：

- 保留 HIR
- 保留 bytecode
- 不把 untyped SSA 作为长期持久化的主干 IR
- 让 SSA 成为热点编译时按需构造的优化 IR
- 把强动态特性建模为 effectful 操作、失效点或 deopt 边界

简化表达就是：

`不要让 SSA 承担 Matlab 的动态语义主线；让 bytecode/interpreter 承担语义，让 SSA 只承担热点优化。`

## 后续可继续细化的方向

接下来最值得继续设计的部分包括：

- HIR 的指令集边界
- 哪些操作必须显式建模为 effectful
- 哪些值和控制流可以进入 SSA
- `eval`、`clear`、`mex` 等操作如何定义为 deopt / invalidation 点
- bytecode 到 typed SSA 的热点提取策略

## 相关文档

相关设计文档见：

- [hir_draft.md](/home/zj/Desktop/Baltam_IR/doc/hir_draft.md)
- [hir_schema.md](/home/zj/Desktop/Baltam_IR/doc/hir_schema.md)
- [execution_strategy.md](/home/zj/Desktop/Baltam_IR/doc/execution_strategy.md)
