# 计划中与思考中的问题

这份笔记记录当前还在计划中、或者仍在持续思考的问题。目标不是一次性定死全部实现，而是先把已经形成的判断、值得继续推进的方向，以及后续 pass 的落点整理清楚，避免后面在 `apply/call`、名字语义、runtime 责任和优化前提上反复摇摆。

如果当前想看“后续还需要系统学习哪些知识点”，可以配合阅读 [learning_notes.md](./learning_notes.md)。如果想看 `ValueId` 类型事实与分派收敛的单独设计，则另见 [value_type_dispatch_design.md](./value_type_dispatch_design.md)。

## 1. 不需要考虑函数会修改入参的情况

目前可调用的函数主要有 3 类：

1. `M` 函数
2. `cpp` 实现的内置函数
3. 插件函数 / bex 函数

其中，`M` 函数的入参在调用时会被复制一次；内置函数如果会修改入参，后续都应标记为 `internal`，不应该暴露给用户。  
而 MATLAB 的 `mex` 函数，官方也明确说明不支持修改入参。

> Input parameters (found in the `prhs` array) are read-only; do not modify them in your MEX function. Changing data in an input parameter can produce undesired side effects.

参考：

- https://ww2.mathworks.cn/help/matlab/matlab_external/gateway-routine.html

## 2. lowering 阶段暂时不用考虑函数调用入参出参应该复制一份的问题

这部分更像是后续解释执行 runtime 需要考虑的事情。当前 `IR` 和 lowering 阶段，暂时不需要在 `apply/call` 周围显式插入参数或结果复制。

当前 `apply/call` 更适合作为语义层节点，负责表达：

- 调用了谁
- 传入了哪些参数
- 产生了哪些结果

它们不应为了“值语义”而在前端 `IR` 里统一插入显式 `copy`。

原因是：

- `M` 语言语义并不是“调用前深拷贝所有参数”
- 物理复制是否发生，取决于 runtime 对象表示和写时复制策略
- 如果前端 `IR` 先把复制固定下来，会过早承诺一种低效实现

因此这部分责任更适合放在：

- bytecode lowering
- 解释执行 runtime
- 后续对象模型和 copy-on-write 触发逻辑

前端 `IR` 当前更应该关心的是调用语义本身，而不是对象复制细节。

## 3. 运行时函数分派的主要难点

运行时函数分派至少有两个核心问题：

1. 名字解析具有优先级顺序
2. 分派结果还可能依赖参数类型

一个粗略但实用的分层方式是：

1. `local`、`nested`、显式 `import` 绑定，这些优先级高，且大多可以静态确定
2. `private` 和 `import namespace`，这类往往可以部分静态分析，但仍依赖上下文
3. 内部已知 builtin 或 runtime primitive，这类通常可以直接确定
4. 运算符调用，这类一般有较高优先级，也更容易归约到已知语义
5. builtin 函数，这类常常还需要结合参数类型继续分派
6. 一般函数查找，这部分最动态

后续如果进入性能优化阶段，可以考虑用以下手段缓解动态分派成本：

- profile
- guard
- 基于热点的 `JIT`
- 类型推导或稳定性分析

关于“给 `ValueId` 增加类型事实以辅助函数分派”的更具体方案，另见 [value_type_dispatch_design.md](./value_type_dispatch_design.md)。

## 4. `eval/addpath/cd/clear` 这类操作会改变执行环境

`eval`、`addpath`、`cd`、`clear` 这类操作会影响名字解析环境或运行时世界状态，因此不应只把它们看成普通调用。

一个可行方向是引入“世界计数”或类似版本号机制，用来描述当前执行环境是否发生变化。

这类机制可以服务于两个目的：

- 判断之前的名字解析、调用缓存和优化假设是否仍然有效
- 在环境发生变化后，触发失效、回退或禁止进一步优化

后续如果有更复杂的执行器，还可以考虑加回调或观察者机制，在世界计数变化时主动标记当前函数的优化结果失效。

## 5. 常量折叠和静态调用优化的前提

常量折叠、直接 `call` 收敛以及其他静态优化，都依赖调用目标在当前上下文中是稳定的。

目前至少需要考虑：

- 当前作用域是否存在 `local` 函数
- 名字是否被局部变量遮蔽

以后还应继续纳入：

- `import`
- `private`
- 路径变化
- 其他会改变名字绑定的环境因素

也就是说，常量折叠和调用优化不只是“表达式本身是否是常量”的问题，还取决于当前名字解析环境是否足够稳定。

## 6. 函数中的 slot 与 SSA 化

在普通函数里，局部变量槽位比脚本名字更适合做 SSA 化，原因主要有这些：

- 不会像脚本那样依赖动态 workspace 名字查找
- 普通调用不会把这些局部变量当成可回写入参去修改
- 参数、返回值和局部变量一旦绑定成 slot，语义通常更稳定

因此，普通函数中的纯局部 slot，原则上很适合作为寄存器化 / SSA lowering 的主要对象。

但这里不能简单地说“函数里的 slot 都可以 SSA 化”，至少还要考虑以下几类例外：

1. 控制流合流。`if / else`、循环之后，同一个变量会有多次定义，需要 `phi` 或等价机制。这不是不能 SSA，而是要做完整 SSA 构造。
2. `global` / `persistent`。它们比脚本 workspace 更稳定，但也不等同于普通局部 slot，因为底层存储不只属于当前 frame。
3. `eval`、`assignin`、`clear` 之类会破坏环境稳定性的操作。这些点会切断许多 SSA 假设。
4. `nested function` / closure。若外层局部被内层捕获，它就不再是纯当前函数私有 slot。
5. 索引或成员更新。例如 `a.x = 1`、`a(i) = 2`。即使 `a` 是局部变量，也往往需要更细的对象更新建模，而不是简单改写成 SSA 名字重绑定。

如果把这件事压缩成更接近 pass 入口条件的说法，更稳妥的结论是：

- 普通函数中的纯局部 slot，原则上很适合 SSA 化
- 脚本中的 workspace 名字不适合作为默认 SSA 主体
- `global`、`persistent`、closure 捕获变量和受动态环境影响的名字，不应默认按纯 SSA 局部处理

如果要把这条原则压缩成一句设计话，可以写成：

> 在函数中，未逃逸、未被特殊语义影响的局部 slot，可作为寄存器化 / SSA lowering 的主要对象；脚本名字、`global`、`persistent`、closure 捕获变量以及受动态环境影响的名字，不应默认按纯 SSA 局部处理。

后面的寄存器化 / SSA 提升 pass，基本就是按这条边界来落地：先在 function 内的纯局部 slot 上做，再逐步扩展到跨 block 的完整 SSA 构造。

## 7. 当前阶段的设计取向

基于以上判断，当前阶段更合适的策略是：

1. 前端 `IR` 继续把 `apply/call` 作为调用语义节点处理。
2. 不把普通调用参数建模成默认可回写对象。
3. 不在前端 `IR` 中统一显式复制参数。
4. 把对象复制、copy-on-write 和更底层存储策略留给 runtime。
5. 把调用分派稳定性、环境变化和优化失效视为独立问题，分别建模。

这样分层更清楚，也更便于后续按需扩展成员访问、索引左值、调用缓存和优化基础设施。

## 8. IR 后续 Pass 计划

这里记录当前 IR 体系下，已经明确值得继续做、但尚未实现的 pass 想法。

重点不是把 pass 设计一次写死，而是先把“为什么值得做”和“最小触发条件”记清楚，避免后续实现时又回到 script / function 名字语义是否稳定的老问题上。

### Pass 1：脚本 `LoadWorkspaceInst` 降级到 `LoadSlotInst`

#### 目标

在 script IR 中，尽可能把原本保守生成的：

- `LoadWorkspaceInst`
  打印时显示为 `load_env`

收敛成：

- `LoadSlotInst`
  打印时显示为 `load_slot`

这样做的核心动机是：当前 script lowering 把名字读取统一建模成 workspace 访问，是一个正确但偏保守的基线。如果后续 pass 能证明某段区间内某个名字的含义已经稳定，就没有必要继续保留动态按名查询。

#### 当前已知的两类机会

##### 1. `def` 之后直到下一次 `def` 之前都只有 `use`

也就是：

- 某个符号先被定义
- 在下一次重新定义之前，后续只发生读取，不再发生新的写入

在这种情况下，这一段 use 链上的 `LoadWorkspaceInst` 可以收敛成读取一个稳定 slot。

可以把它理解成 script 层的一个局部名字稳定区间分析：

- `StoreWorkspaceInst @a`
- 后面若干次 `LoadWorkspaceInst @a`
- 直到下一次 `StoreWorkspaceInst @a` 之前

这一段读取都可以改写成针对同一个 slot 的 `load_slot`。

##### 2. 脚本在函数中被调用

当 script 是在函数上下文中被调用时，script 中的名字语义可能已经不再需要完整保留为“开放 workspace 查询”。如果调用边界已经把相关符号收紧为单一含义，那么脚本内部对这些名字的读取也有机会进一步收敛成 `load_slot`。

这一类场景本质上依赖更强的调用上下文信息，但它值得单独记下来，因为它和“裸 script 顶层执行”不是同一类保守性要求。

#### 预期收益

- 减少动态 workspace 查询
- 让 script 与 function 在局部稳定区间内共享更多 IR 形状
- 为后续调用分派收敛提供更强的前提

#### 需要注意的问题

- 这个 pass 不能只看名字是否出现，还要看 def-use 的区间边界
- 若后续引入 `global`、`persistent`、`eval` 或其他动态名字特性，需要重新评估可收敛范围
- 这里当前只明确记录 `LoadWorkspaceInst -> LoadSlotInst`，不等价于所有 `StoreWorkspaceInst` 也可以直接改写

### Pass 2：脚本中的 `apply` 降级到 `call`

#### 目标

在 script IR 中，当前 `A(...)` 会保守 lower 成 `apply`，因为在一般脚本场景下，名字 `A` 的含义未必稳定，可能是函数，也可能被 workspace 中的同名变量遮蔽。

如果后续 pass 已经证明某个 script 名字在当前位置只能有一个含义，那么：

- `apply`

就可以进一步收敛成：

- `call`

#### 适用前提

这个 pass 依赖“脚本名字语义已经稳定”这一前提。换句话说，它通常不会独立出现，而是建立在前一个 pass 或同类名字稳定性分析之上。

只有当某个脚本符号已经不再需要保留“运行时再决定它是函数还是变量”的歧义时，`apply` 才能安全改写成 `call`。

#### 预期收益

- 让 script 中可静态确定的调用形状与 function 对齐
- 降低后续执行层在调用点做动态分派的成本
- 为 inline cache、调用目标解析缓存、后续 JIT 降低不必要的保守性

#### 需要注意的问题

- 这个 pass 的正确性依赖于名字含义稳定性，而不是语法形状本身
- 如果某个名字仍可能被 workspace 中的变量遮蔽，就必须保留 `apply`
- 因此它更像是“名字消歧 pass”的后半段，而不是一个单纯的指令替换 pass

### Pass 3：对可静态确定目标的 `M` 函数做内联

#### 目标

在 function IR 中，对一小类已经能证明安全且收益明确的 `M` 函数调用直接展开函数体，减少调用开销，并为后续常量传播、DCE 和 SSA 清理提供更大空间。

#### 第一阶段的最小适用前提

初版内联 pass 不追求“一上来就支持所有 `M` 函数”，而是先把最容易证明正确的一小类场景收进来。更合适的前提大致是：

- `callee` 可以静态分派，最好已经是 `dispatch_type = MFunction` 的 `call mfunc`，
  至少也应是名字绑定稳定的直接 `call`
- `caller` 和 `callee` 都是 `FunctionUnit`，暂不考虑 `script`
- `callee` 没有复杂控制流，初版先限制为单一线性 block，不含 `branch`、循环和多出口 `return`
- `callee` 不包含 `eval`、`assignin`、`clear`、`addpath`、`cd` 等会破坏环境稳定性的操作
- `callee` 不涉及 `global`、`persistent`
- `callee` 不涉及 `nested function`、closure 捕获或其他会让局部 slot 逃逸的情况
- `callee` 不使用 `varargin` / `varargout` 这类变参形态
- 初版先排除递归和互递归
- 可以额外加一个体量阈值，例如只内联指令数较小的函数，避免代码膨胀

如果把它压缩成一句判断，大致可以写成：

> 只有当调用目标已经静态确定、函数体是纯局部 slot 驱动的简单直线型代码、且不依赖动态环境语义时，才进入第一阶段 `M` 函数内联候选集。

#### 基本做法

- 在 caller 中为 callee 的局部 slot 建立一份重命名后的映射
- 把 callee 的直线型指令复制到调用点附近
- 把形参改写成调用点实参，把返回值改写成调用点结果
- 内联完成后，再跑一轮 DCE、copy propagation 或 SSA 清理

#### 这样收窄的原因

- 当前 function 中的局部 slot 语义最稳定，最适合作为第一批内联对象
- script 名字、动态环境操作、`global` / `persistent`、closure 都会显著提高正确性成本
- 先限制为无复杂控制流，可以避免一开始就引入 block 拼接、`phi` 合流和更多 CFG 变换细节

### Pass 4：函数中纯局部 `load_slot` / `store_slot` 的寄存器化与 SSA 提升

#### 目标

在 function IR 中，把满足前提的 `load_slot` / `store_slot` 收敛成更接近寄存器式 `ValueId` 的数据流，减少 frame slot 往返，并为后续常量传播、类型分析、DCE 和算术优化提供更干净的输入。

这里说的“寄存器化”不一定要求主 `IR` 立刻整体改写成全局常驻 SSA。更实际的第一步是：

- 先在单个 basic block 或足够稳定的 region 内，做局部 `load/store` 消除
- 再扩展到跨 block 的完整 SSA promotion / `phi` 构造

#### 第一阶段的最小适用前提

初版更适合先处理一小类纯函数局部场景：

- 当前 unit 是 `FunctionUnit`，而不是 `ScriptUnit`
- 只处理参数、局部变量、返回值这类普通 frame slot；不处理 `hidden slot`
- 对应 slot 不是 `global`、`persistent`
- 对应 slot 没有被 `nested function` / closure 捕获，也不存在其他逃逸路径
- 当前 basic block 或 region 中不包含 `eval`、`assignin`、`clear`、`addpath`、`cd` 等会破坏环境稳定性的操作
- 不涉及 `a.x = ...`、`a(i) = ...` 这类需要更细对象更新或别名建模的写入
- 如果当前只做单 block 版本，则不允许依赖跨 block 的定义合流；若要跨 block 提升，就必须显式引入 `phi` 或等价机制

#### 基本做法

- 为每个候选 slot 维护当前可用的 reaching value
- `load_slot` 若能命中当前已知值，则直接改写成该值
- `store_slot` 不再急着物化成 frame 写入，而是先更新该 slot 的当前值状态
- 对没有后续观察者的冗余 `store_slot` 做删除
- 在 region 出口、显式 `return` 或其他需要物化 frame 状态的位置，再决定是否回写 slot
- 扩展到跨 block 时，再补 `phi`、live-in / live-out 和支配关系处理

#### 需要注意的问题

- “单个 basic block 里的局部寄存器化”和“整个函数的完整 SSA 化”不是同一难度级别，前者可以先落地
- 普通调用本身不必自动视为 slot 提升屏障；真正需要保守处理的，是会破坏环境稳定性或对象语义边界的点
- 这条 pass 不应是 all-or-nothing；更合理的是按 slot、按 region、按 block 渐进启用
- 函数内联之后通常会暴露更多这类机会，因此它很适合作为内联后的清理与强化 pass

### Pass 5：删除未被调用的 local 函数

#### 目标

删除当前文件里已经可以证明不会再被引用的 local 函数，减少无用 `FunctionUnit`。

#### 基本思路

- 从 `mfile.entry_unit` 出发遍历当前文件内的调用图
- 只跟踪 `CallInst(Local)` 这类已经静态确定目标的调用边
- 被遍历到的 local 函数视为可达，未被遍历到的视为可删除

#### 适用范围

这条 pass 更适合先用于函数文件。

原因是当前函数文件中的 local 调用已经会显式 lower 成
`dispatch_type = MFunction` 的 `CallInst`，可达性边比较清楚。

脚本文件中的 local 函数原则上也可以删除，因为文件外部无法直接访问它们；但脚本场景要额外排除几类情况：

- 脚本内部仍存在可能命中该 local 的 `apply`
- 该 local 以函数句柄等形式逃逸出当前文件

也就是说，脚本 local 不是天然不能删除，而是删除条件比函数文件更强。

### Pass 6：通用 CFG simplify

#### 目标

在不改变语义的前提下，清理 lowering 或其他 pass 留下的冗余 CFG 结构，减少解释执行时的
block dispatch 成本，并给后续分析提供更小、更规整的图。

这条 pass 不应是 `for` 专用优化。`for` 当前保留
`preheader / header / body / latch / end` 的 canonical form，是为了让 `break / continue`、
loop 分析、SSA 构造和后续循环优化更容易识别。是否减少 block 数量，应交给通用
CFG 清理规则处理。

#### 第一阶段可以覆盖的规则

- 删除不可达 block。
- 合并空的直通 block，例如只包含 `br label %next` 且没有必须保留的语义标记。
- 折叠连续跳转，例如 `A -> B -> C` 中 `B` 只是空跳转块时，把 `A` 改跳 `C`。
- 合并只有一个 predecessor、一个 successor，且不会改变源码注释 / debug 边界语义的 block。
- 删除或更新过时的 predecessor / successor 边，保证 CFG 边和 terminator 目标一致。

#### 与 `for` lowering 的关系

`for` lowering 仍应先生成规范的五块形状：

```text
for.preheader -> for.header -> for.body -> for.latch -> for.header
                         \-> for.end
```

后续如果某个具体循环没有 `break / continue`，且 `latch` 只包含简单自增和回跳，
CFG simplify 可以选择性地把 `body -> latch` 这类结构压缩掉。

但这个优化不应在 lowering 阶段做，原因是：

- lowering 阶段更重要的是输出稳定、容易验证的语义形状
- `continue` 的自然目标是 `latch`，提前合并会增加后续支持 `continue` 的复杂度
- loop 分析、类型推导、SSA 提升等 pass 更容易消费 canonical loop form
- block 数量是否真的影响性能，需要等解释器或 bytecode 执行路径稳定后再评估

#### 运行时机

这条 pass 可以有两个使用点：

- lowering 后，作为进入解释执行或打印前的可选清理
- 内联、DCE、常量折叠等变换后，作为通用 cleanup pass 重复运行

第一阶段更建议默认保守：先提供 pass 和测试，不急着改变当前 smoke test 的打印基线。
等需要面向执行性能时，再决定是否默认启用。

### Pass 7：常量折叠

#### 目标

把已经能静态证明结果的表达式提前求值，减少运行时计算，并为死分支消除、DCE 和
CFG simplify 暴露更多机会。

#### 第一阶段可以覆盖的规则

- 简单 `ConstInst` 传播到只依赖常量的纯运算。
- 对已知纯且语义稳定的 `UnaryInst / BinaryInst` 做折叠。
- 对比较结果做折叠，例如两个已知常量比较后生成 logical 常量。
- 对不影响动态语义的内部 helper，可在其语义完全固定后增加专门规则。

#### 安全边界

- 不能因为语法是 `add / colon / sin` 就直接折叠；Matlab 调用和运算可能动态分派。
- `ApplyInst` 默认不能折叠，除非前置 pass 已经证明调用目标和参数语义稳定。
- `CallInst(Direct)` 也不天然等于可折叠；只有已知 pure builtin / internal helper 才能折叠。
- 如果 `ValueId` 的类型事实仍是 `unknown`，且折叠规则依赖具体类型，就必须保守跳过。

也就是说，这条 pass 的关键前提不是“看起来像常量表达式”，而是：

> 操作语义已经静态确定，并且输入都是可用常量事实。

### Pass 8：复制消除 / copy propagation

#### 目标

消除 IR 层面的冗余 `CopyInst` 和单纯值别名，减少无意义的中间 `ValueId`，并简化后续
DCE、常量折叠和 SSA 提升的输入。

这里的“复制消除”只指 IR 数据流里的 `copy` 或等价别名传播，不是运行时对象的
copy-on-write，也不是函数调用参数的物理复制。

#### 第一阶段可以覆盖的规则

- `x = copy y` 后，若 `x` 的所有 use 都可安全替换为 `y`，则替换 use 并删除该 `CopyInst`。
- 多级 copy 链压缩，例如 `x = copy y; z = copy x` 可直接让 `z` 使用 `y`。
- 常量 copy 可与常量传播协同，让后续常量折叠直接看到原始常量。

#### 需要注意的问题

- 替换 use 时必须维护 `ValueTable`、def-use 信息和 verifier 约束。
- 如果后续 `CopyInst` 被赋予额外语义，例如 materialize、guarded copy 或对象边界，
  就不能再按普通别名处理。
- 当前阶段更适合先做局部、显式 use 列表或扫描式替换版本，等 def-use 基础设施稳定后再扩展。

### Pass 9：死分支消除

#### 目标

当 `BranchInst` 的条件已经能静态确定时，把条件分支改写成无条件跳转，并删除不可达路径。

这条 pass 通常吃常量折叠的结果。例如：

```text
%c = const true
br %c, label %then, label %else
```

可以改写成：

```text
br label %then
```

然后交给 CFG simplify 删除不可达的 `else` 路径。

#### 第一阶段可以覆盖的规则

- 条件直接是 logical 常量。
- 条件来自已折叠出的 logical `ConstInst`。
- `if` / loop header 中明显恒真或恒假的条件。

#### 安全边界

- Matlab 条件表达式本身的 truthiness 规则可能不只是 scalar bool，不能擅自把任意常量对象当作
  C/C++ 风格布尔值处理。
- 如果条件值来自 `ApplyInst` 或动态 `CallInst`，即使看起来名字是 `true/false` 相关函数，也不能折叠。
- 对循环分支做消除时要特别注意是否会改变 loop 结构；删除后必须立刻跑 CFG 验证和 CFG simplify。

### Pass 10：死代码消除

#### 目标

删除没有 observable effect、且结果也不再被使用的指令，减少 IR 体量。

#### 第一阶段可以覆盖的规则

- 删除未使用结果的 `ConstInst`。
- 删除未使用结果且无副作用的 `CopyInst`。
- 删除未使用结果且已知 pure 的 `UnaryInst / BinaryInst`。
- 删除不可达 block 中的所有指令，这部分通常配合 CFG simplify 完成。

#### 安全边界

- `StoreSlotInst / StoreWorkspaceInst` 不能仅因为结果为空就删除；它们表达写入。
- `ApplyInst / CallInst` 默认不能删除，因为可能有副作用、抛错、修改动态环境或触发分派逻辑。
- `LoadWorkspaceInst` 是否可删除需要谨慎：即使只是读，也可能涉及动态 workspace 查询和错误行为。
- 删除指令后必须同步更新 `ValueTable`、CFG 状态和后续 use 信息。

第一版 DCE 更适合作为保守 pass：只删除明确 pure、明确无 use 的内容。等 effect model 更细以后，
再逐步扩大可删范围。

### 粗略顺序

当前更合理的实现顺序是：

1. 先做 script 名字稳定区间分析
2. 基于稳定结果把部分 `LoadWorkspaceInst` 收敛成 `LoadSlotInst`
3. 再基于同一份稳定信息，把部分 `apply` 收敛成 `call`
4. 跑一轮基础 cleanup：复制消除、常量折叠、死分支消除、CFG simplify、DCE
5. 再对满足前提的静态 `call` / `call mfunc` 做函数内联
6. 内联后重复基础 cleanup，吃掉内联暴露出来的 copy、常量、死分支和死代码
7. 再对 function 内的纯局部 slot 做寄存器化 / SSA 提升，先从单 block 或稳定 region 开始
8. 最后做基于 `MFunction` 调用可达性的 local function DCE；若内联后出现新的死 local 函数，可以再重复一轮
9. 每个会改 CFG 的 pass 之后，都可以再跑一轮 CFG simplify 作为 cleanup

原因很简单：第二个 pass 依赖的前提，和第一个 pass 证明的其实是同一类事实；函数内联又依赖调用目标已经先收敛成足够稳定的静态 `call`；而寄存器化 / SSA 提升则最适合放在内联之后，去吃掉内联额外暴露出来的局部 slot 数据流机会。
