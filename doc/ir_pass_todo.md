# IR 后续 Pass TODO

## 目标

本文只记录当前 IR 体系下，已经明确值得继续做、但尚未实现的 pass 想法。

这里的重点不是把 pass 设计一次写死，而是先把“为什么值得做”和“最小触发条件”记清楚，
避免后续实现时又回到 script / function 名字语义是否稳定的老问题上。

## Pass 1：脚本 `LoadWorkspaceInst` 降级到 `LoadSlotInst`

### 目标

在 script IR 中，尽可能把原本保守生成的：

- `LoadWorkspaceInst`
  打印时显示为 `load_env`

收敛成：

- `LoadSlotInst`
  打印时显示为 `load_slot`

这样做的核心动机是：当前 script lowering 把名字读取统一建模成 workspace 访问，是一个正确但偏保守的基线。
如果后续 pass 能证明某段区间内某个名字的含义已经稳定，就没有必要继续保留动态按名查询。

### 当前已知的两类机会

#### 1. `def` 之后直到下一次 `def` 之前都只有 `use`

也就是：

- 某个符号先被定义
- 在下一次重新定义之前，后续只发生读取，不再发生新的写入

在这种情况下，这一段 use 链上的 `LoadWorkspaceInst` 可以收敛成读取一个稳定 slot。

可以把它理解成 script 层的一个局部名字稳定区间分析：

- `StoreWorkspaceInst @a`
- 后面若干次 `LoadWorkspaceInst @a`
- 直到下一次 `StoreWorkspaceInst @a` 之前

这一段读取都可以改写成针对同一个 slot 的 `load_slot`。

#### 2. 脚本在函数中被调用

当 script 是在函数上下文中被调用时，script 中的名字语义可能已经不再需要完整保留为“开放 workspace 查询”。
如果调用边界已经把相关符号收紧为单一含义，那么脚本内部对这些名字的读取也有机会进一步收敛成 `load_slot`。

这一类场景本质上依赖更强的调用上下文信息，但它值得单独记下来，因为它和“裸 script 顶层执行”不是同一类保守性要求。

### 预期收益

- 减少动态 workspace 查询
- 让 script 与 function 在局部稳定区间内共享更多 IR 形状
- 为后续调用分派收敛提供更强的前提

### 需要注意的问题

- 这个 pass 不能只看名字是否出现，还要看 def-use 的区间边界
- 若后续引入 `global`、`persistent`、`eval` 或其他动态名字特性，需要重新评估可收敛范围
- 这里当前只明确记录 `LoadWorkspaceInst -> LoadSlotInst`，不等价于所有
  `StoreWorkspaceInst` 也可以直接改写

## Pass 2：脚本中的 `apply` 降级到 `call`

### 目标

在 script IR 中，当前 `A(...)` 会保守 lower 成 `apply`，因为在一般脚本场景下，名字 `A` 的含义未必稳定，
可能是函数，也可能被 workspace 中的同名变量遮蔽。

如果后续 pass 已经证明某个 script 名字在当前位置只能有一个含义，那么：

- `apply`

就可以进一步收敛成：

- `call`

### 适用前提

这个 pass 依赖“脚本名字语义已经稳定”这一前提。换句话说，它通常不会独立出现，而是建立在前一个 pass
或同类名字稳定性分析之上。

只有当某个脚本符号已经不再需要保留“运行时再决定它是函数还是变量”的歧义时，`apply` 才能安全改写成 `call`。

### 预期收益

- 让 script 中可静态确定的调用形状与 function 对齐
- 降低后续执行层在调用点做动态分派的成本
- 为 inline cache、调用目标解析缓存、后续 JIT 降低不必要的保守性

### 需要注意的问题

- 这个 pass 的正确性依赖于名字含义稳定性，而不是语法形状本身
- 如果某个名字仍可能被 workspace 中的变量遮蔽，就必须保留 `apply`
- 因此它更像是“名字消歧 pass”的后半段，而不是一个单纯的指令替换 pass

## 粗略顺序

当前更合理的实现顺序是：

1. 先做 script 名字稳定区间分析
2. 基于稳定结果把部分 `LoadWorkspaceInst` 收敛成 `LoadSlotInst`
3. 再基于同一份稳定信息，把部分 `apply` 收敛成 `call`

原因很简单：第二个 pass 依赖的前提，和第一个 pass 证明的其实是同一类事实。
