# 循环 Lowering 设计

## 目标

本文记录当前 `src/ir/ir_lowering.cpp` 中循环语句的 lowering 规则。

当前已覆盖 `for` 循环、`while` 循环、循环内 `break / continue`，以及 `for / while`
的嵌套组合。

## `for` 循环

### 目标

当前目标不是完整覆盖 Matlab `for` 的全部语义，而是先把以下最小闭环打通：

- `for i = expr ... end`
- 迭代表达式为普通表达式，例如 `1:10`
- 循环变量为名字
- 循环体为普通语句列表
- 输出可验证、可打印的 non-SSA CFG IR

示例输入：

```matlab
s = 0;
for i = 1:10
    s = s + i;
end
```

### 1. 基本 CFG 形状

`for` lowering 使用五块 canonical CFG。这里的“五块”对应 5 个 loop basic block：

```text
entry
  -> for.preheader

for.preheader
  -> for.header

for.header
  -> for.end   // done
  -> for.body  // not done

for.body
  -> for.latch

for.latch
  -> for.header

for.end
  -> 后续 continuation
```

当前 `test2.m` 中循环之后没有后续语句，所以 `for.end` 里接隐式 `ret`。

### 2. `1:10` 的表达式 lowering

Matlab 的冒号表达式 `1:10` 在 AST 中是 `node_colon`。

`colon` 是 Matlab 表面语义中的特殊运算符。它已经确定是一次运算符调用，因此应 lower 成
`CallInst`，但它不是 lowering 内部 helper，仍可能需要根据参数类型或后续规则动态分派。

因此当前 lowering 不把它标成 `internal`，也不把它特殊折叠成常量 range：

```text
%lo    = const 1
%hi    = const 10
%range = call @colon(%lo, %hi)
```

因此 `for i = 1:10` 的 lowering 分两层：

1. `1:10` 先 lower 成普通 `call @colon(...)`，它是 call 节点，但不是 `internal`
2. `for` 协议再消费该 iterable，生成静态内部 helper `foreach_init / foreach_iterate`

### 3. Matlab `for` 的迭代快照语义

Matlab 的 `for i = a ... end` 会在进入循环时确定当前循环的迭代来源。循环体内对
`i` 或 `a` 的后续赋值，不会改变这个已经建立的迭代协议：

- 最大循环次数不会因为循环体内修改 `i` 或 `a` 而改变。
- 每次进入循环体前写给 `i` 的当前迭代值不会因为上一轮循环体内修改 `i` 或 `a`
  而改变。
- 循环体内对 `a` 的赋值仍然是普通用户变量写入；后续普通表达式读取 `a` 时可以看到
  新值，但当前 `for` 的迭代次数和迭代值不再重新读取 `a` 来决定。
- 循环体内对 `i` 的赋值只影响该轮剩余语句及循环结束后的普通变量值；下一轮开始时，
  `for` 会重新写入当前迭代值。

因此 lowering 必须把 `for` 迭代表达式放在 `for.preheader` 中求值一次，并用内部
迭代状态驱动后续 header/body/latch。不能在每一轮重新读取用户变量 `a`，也不能用
用户变量 `i` 本身作为循环推进状态。

### 4. 两个内部 helper

当前 `for` 协议使用两个内部 helper：

- `foreach_init(iterable) -> (state, max_iter)`
- `foreach_iterate(state, iter_index) -> current_value`

其中：

- `foreach_init`
  初始化迭代状态，并返回最大迭代次数。`state` 和 `max_iter` 都是 lowering/runtime
  内部值，不是 Matlab 用户可见变量。
- `foreach_iterate`
  根据内部 `state` 和当前 `iter_index` 读取当前迭代值。这里的“当前值”由 runtime
  helper 按 Matlab `for` 规则决定，例如矩阵输入按列返回当前列。迭代下标递增由
  lowering 显式生成，不由 `foreach_iterate` 自己更新。

内部 helper 的返回类型由 helper 签名静态决定：

- `state`
  是循环不变量，类型是 `extern` 外部对象，用于表示 runtime 持有的不透明状态。
- `max_iter`
  是循环不变量，类型是内部整型标量，例如 `int64` index。
- `iter_index`
  是循环携带状态，每轮都会变化，但同样是内部整型标量，例如 `int64` index。

这些内部值不参与 Matlab 用户级名字查找，也不参与 Matlab 运算符重载。

这两个 helper 在 IR 中都是 `CallInst(Direct, dispatch_type = Internal)`，打印为
统一的 `internal.xxx` 静态分派目标：

```text
call @internal.foreach_init(...)
call @internal.foreach_iterate(...)
```

### 5. 循环携带状态

如果使用可覆盖的 non-SSA lowering，可以直接覆盖同一个循环下标值：

```text
foreach_iter_index = foreach_iter_index + 1
```

当前 IR 中 `ValueId` 是单定义值，不能被重新定义。因此真正随循环变化的
`iter_index` 必须放在 slot 或等价的内部可变存储中。

语义上，`state / max_iter / iter_index` 三者的可变性不同：

```text
state      : loop-invariant internal value
max_iter   : loop-invariant internal int64 scalar
iter_index : loop-carried internal int64 scalar
```

因此更精确的目标形态是只为 `iter_index` 创建 lowering 内部
`internal_local` slot：

```text
%slot1 = internal_local @__foreach_iter_index : int64
```

`state` 和 `max_iter` 来自 `foreach_init`，在当前循环生命周期内值不变，后续 block
可以直接使用这两个 `ValueId`。如果某个后端暂时只能从 slot 读取跨 block 内部值，
也可以保守地把 `state` 和 `max_iter` 落到 internal local slot 中；但这只是实现
约束，不是 Matlab `for` 语义上的必要条件。

`iter_index` 虽然需要内部可变存储来跨 `for.latch -> for.header` 传递下一轮值，
但它不是动态 Matlab 变量。它的 slot 应通过 `SlotAttrs::fixed_type` 声明为固定
`int64 scalar`：

```text
slot type = InternalLocal
is_mutable = true
fixed_type = Int64Scalar
```

因此每次 `load_slot %iter_index_slot` 的结果类型都稳定为 `int64`，不会被写成
`unknown`，也不会被循环体中的用户代码收窄或污染。`store_slot` 写回该 slot 的值
也应满足这个固定类型约束。

这里不使用 `hidden slot`，因为当前 `HiddenRole` 有唯一性约束，适合 `WorkspaceHandle`
这类单例角色，不适合每个循环都创建多份的状态槽。当前做法是
`internal_local slot`，直接通过 `Slot::InternalLocal` 表示 lowering 内部状态。

### 6. 内部 index 运算

`iter_index` 和 `max_iter` 都是 lowering/runtime 内部整型标量。二者之间的比较和
`iter_index` 的自增完全是静态分派的内部 primitive，不适用 Matlab 用户级运算符
重载规则。

也就是说，header 中的结束判断不应被解释成 Matlab 表面语义的：

```matlab
lt(max_iter, iter_index)
```

latch 中的递增也不应被解释成 Matlab 表面语义的：

```matlab
plus(iter_index, 1)
```

它们应建模为 IR 内部 primitive，例如：

```text
%done = internal.cmp_gt %iter_index, %max_iter
%next = internal.add %iter_index, 1
```

当前实现使用 `BinaryInst(dispatch_type = Internal)` 表示这两个内部静态分派运算。
打印器显示为 `internal.cmp_gt` / `internal.add`，后续解释器、优化器和 verifier
不能把它们按 Matlab overloadable operator 处理。

### 7. 各 block 语义

#### 7.1 `for.preheader`

职责：

- lower 迭代表达式
- 调用 `foreach_init`
- 得到循环不变量 `state / max_iter`
- 初始化迭代下标为 `1`
- 跳转到 `for.header`

形态：

```text
%range = call @colon(...)
([%state, extern], [%max_iter, int64]) = call @internal.foreach_init(%range)
[%initial_index, int64] = const 1
store_slot %iter_index_slot, %initial_index
br label %for.header
```

#### 7.2 `for.header`

职责：

- 读取当前 `iter_index`
- 判断是否结束
- 结束则跳 `for.end`，否则进入 `for.body`

当前结束条件为：

```text
done = max_iter < iter_index
```

也就是当 `iter_index` 已经超过 `max_iter` 时结束。

形态：

```text
[%iter_index, int64] = load_slot %iter_index_slot
[%done, logical] = internal.cmp_gt %iter_index, %max_iter
br %done, label %for.end, label %for.body
```

这里 `internal.cmp_gt` 是静态内部比较，不参与 Matlab `gt/lt` 运算符重载。

#### 7.3 `for.body`

职责：

- 根据 `state` 和当前 `iter_index` 取当前迭代值
- 把当前值写给循环变量
- lower 循环体
- 普通 fallthrough 跳转到 `for.latch`
- `continue` 也跳转到 `for.latch`，从而继续执行内部迭代下标自增
- `break` 跳转到 `for.end`，直接离开当前循环

形态：

```text
[%iter_index, int64] = load_slot %iter_index_slot
[%current_value, unknown] = call @internal.foreach_iterate(%state, %iter_index)
store_env %env, @i, %current_value
```

脚本中循环变量 `i` 仍然是 workspace 名字，因此写回为：

```text
store_env %env, @i, %current_value
```

示例循环体 `s = s + i` lower 为：

```text
%s = load_env %env, @s
%i = load_env %env, @i
%sum = add %s, %i
store_env %env, @s, %sum
```

这里 `s + i` 是用户程序中的 Matlab 加法，仍然可以按表面语义动态分派或等待后续类型
推导；它和内部 `iter_index` 自增不是同一类运算。

#### 7.4 `for.latch`

职责：

- 读取当前迭代下标
- 加一
- 写回迭代下标 slot
- 回跳 `for.header`

形态：

```text
[%iter_index, int64] = load_slot %iter_index_slot
[%next_index, int64] = internal.add %iter_index, 1
store_slot %iter_index_slot, %next_index
br label %for.header
```

这里 `internal.add` 是静态内部自增，不参与 Matlab `plus` 运算符重载。

#### 7.5 `for.end`

职责：

- 作为循环退出后的 continuation
- 如果源码中循环后还有语句，则继续 lower 后续语句
- 如果没有后续语句，则由通用逻辑补隐式 `ret`

### 8. `test2.m` 的目标 IR 形态

省略源码注释后，`test/m/test2/test2.m` 的核心 IR 应收敛为：

```text
script @test2 {
  ; slots:
  %test2_env = hidden(env) @test2_env
  %slot1 = internal_local @__foreach_iter_index : int64

entry:
  [%0, double] = const 0
  store_env %test2_env, @s, %0
  br label %for.preheader

for.preheader:
  [%2, double] = const 1
  [%3, double] = const 10
  [%1, unknown] = call @colon(%2, %3)
  ([%4, extern], [%5, int64]) = call @internal.foreach_init(%1)
  [%6, int64] = const 1
  store_slot %slot1, %6
  br label %for.header

for.header:
  [%7, int64] = load_slot %slot1
  [%8, logical] = internal.cmp_gt %7, %5
  br %8, label %for.end, label %for.body

for.body:
  [%9, int64] = load_slot %slot1
  [%10, unknown] = call @internal.foreach_iterate(%4, %9)
  store_env %test2_env, @i, %10
  [%12, unknown] = load_env %test2_env, @s
  [%13, unknown] = load_env %test2_env, @i
  [%14, unknown] = add %12, %13
  store_env %test2_env, @s, %14
  br label %for.latch

for.latch:
  [%15, int64] = load_slot %slot1
  [%16, int64] = const 1
  [%17, int64] = internal.add %15, %16
  store_slot %slot1, %17
  br label %for.header

for.end:
  ret
}
```

### 9. 当前实现边界

当前 `for` lowering 仍有明确边界：

- 循环变量只支持名字形式。
- 支持循环体内 `break / continue`：
  - `break` 通过当前 loop context 跳转到 `for.end`
  - `continue` 通过当前 loop context 跳转到 `for.latch`
- `node_colon` 当前统一 lower 为普通 `call @colon(...)`，不做常量折叠，也不标记为 `internal`。
- 当前实现已经只把 `iter_index` 落到 internal local slot，并通过
  `SlotAttrs::fixed_type = Int64Scalar` 声明固定类型。
- 当前实现已经把 `foreach_iterate` 调整为显式接收 `state, iter_index`。
- `foreach_iterate` 已负责按 Matlab 规则返回当前迭代值，包括矩阵输入时返回当前列。
- 当前实现已经把 header 比较和 latch 自增 lowering 为 `internal.cmp_gt` /
  `internal.add`，底层是 `BinaryInst(dispatch_type = Internal)`，不再复用用户级
  动态 `cmp.lt / add`。
- `state / max_iter` 由 internal helper 签名提供 `extern / int64` 类型事实。
- 剩余工作主要是把 helper ABI 文档化并补齐边界测试，例如空迭代源、不同形状输入和
  runtime 错误路径。

### 10. 测试覆盖

当前对应测试：

- `test/m/test2/test2.m`
- `test/m/test2/test2_1.m`
- `test/m/test2/test2_2.m`
- `test/m/test2/test2_3.m`
- `test/smoke_test/syntax/test2_smoke.cpp`
- `test/smoke_test/syntax/test2_1_smoke.cpp`
- `test/smoke_test/syntax/test2_2_smoke.cpp`
- `test/smoke_test/syntax/test2_3_smoke.cpp`

测试重点：

- `test2` 是脚本单元。
- 生成 `entry + 5` 个 basic block。
- 打印包含 `for.preheader / for.header / for.body / for.latch / for.end`。
- `1:10` 生成一次非 internal 的 `colon` call。
- `for` 协议生成一次 `internal.foreach_init` 和一次 `internal.foreach_iterate`。
- 循环体 `s = s + i` 通过 `load_env / add / store_env` 表达。
- `test2_1` 覆盖 `continue -> for.latch` 和 `break -> for.end`。
- `test2_2` 覆盖嵌套 `for`，要求内外两层各自生成独立的五块 loop CFG、
  `foreach_init / foreach_iterate` 协议和 internal iter_index slot。
- `test2_3` 覆盖嵌套循环中最近一层 loop context 的选择：内层 `continue` 跳内层
  `for.latch.1`，外层 `break` 跳外层 `for.end`。

## `while` 循环

`while` lowering 保持和 `for` 一致的显式 CFG 风格，并复用当前
`loop_stack_` / `LoopControlContext` 机制。

### 1. 基本 CFG 形状

当前采用四块结构：

```text
current
  -> while.header

while.header
  -> while.body  // cond true
  -> while.end   // cond false

while.body
  -> while.latch

while.latch
  -> while.header

while.end
  -> 后续 continuation
```

相比只使用 `while.header / while.body / while.end` 三块，单独保留 `while.latch`
有两个好处：

- `continue` 和循环体普通 fallthrough 可以统一汇合到 latch，再回到 header 重新求值条件。
- 后续如果需要插入循环计数、profile hook、debug hook 或 cleanup，latch 有稳定落点。

### 2. 各 block 语义

#### 2.1 `while.header`

职责：

- 每轮重新 lower 并求值 `while` 条件表达式
- 条件为真进入 `while.body`
- 条件为假进入 `while.end`

形态：

```text
while.header:
  %cond = ...
  br %cond, label %while.body, label %while.end
```

当前沿用 `if` 的条件 lowering 规则，直接把条件表达式结果交给 `BranchInst`。
后续如果需要严格建模 Matlab 条件 truthiness，可以统一在 `if / while` 条件位置插入
内部 helper，例如 `internal.to_logical_condition`。

#### 2.2 `while.body`

职责：

- lower 用户循环体
- 普通 fallthrough 跳转到 `while.latch`

形态：

```text
while.body:
  ...
  br label %while.latch
```

#### 2.3 `while.latch`

职责：

- 作为普通 fallthrough 和 `continue` 的汇合点
- 回跳 `while.header`，让条件在下一轮重新求值

形态：

```text
while.latch:
  br label %while.header
```

#### 2.4 `while.end`

职责：

- 作为循环退出后的 continuation
- 如果源码中循环后还有语句，则继续 lower 后续语句
- 如果没有后续语句，则由通用逻辑补隐式 `ret`

### 3. `break / continue`

`while` lowering 应复用当前 loop context 栈：

```text
break_target    = while.end
continue_target = while.latch
```

因此：

- `break` 跳到 `while.end`
- `continue` 跳到 `while.latch`，再统一回到 `while.header` 重新求值条件

这里和 `for` 的区别是：`for.continue` 跳到 `for.latch` 是为了执行内部迭代下标自增；
`while.continue` 跳到 `while.latch` 则是为了保持 CFG 形状一致，并为后续 latch hook
保留稳定插入点。

### 4. lowering 伪代码

```cpp
void IRLowerer::lower_while_stmt(const std::shared_ptr<if_flow>& while_node) {
    BasicBlock* header = unit->create_block("while.header", source_span_from(while_node));
    BasicBlock* body = unit->create_block("while.body", source_span_from(while_node->tl()));
    BasicBlock* latch = unit->create_block("while.latch", source_span_from(while_node));
    BasicBlock* end = unit->create_block("while.end", source_span_from(while_node));

    append_goto_from_current_block_to(header);

    builder_.set_insert_point(header);
    ValueId condition = lower_expr(while_node->cond());
    append_branch(condition, body, end);

    builder_.set_insert_point(body);
    const ScopedLoopContext loop_context(*this, {end, latch});
    lower_stmt(while_node->tl());
    append_goto_to_latch_if_current_block_is_open();

    builder_.set_insert_point(latch);
    append_goto(latch, header);

    builder_.set_insert_point(end);
}
```

### 5. 测试覆盖

`test/m/test3/test3.m` 是简单 while 样例：

```matlab
i = 0;
s = 0;
while i < 10
    i = i + 1;
    s = s + i;
end
```

对应 smoke test：

- `test/smoke_test/syntax/test3_smoke.cpp`
- `test/m/test3/test3_1.m` 和 `test/smoke_test/syntax/test3_1_smoke.cpp` 进一步覆盖
  `while` 循环体内 `continue / break` 的目标选择
- `test/m/test3/test3_2.m` 和 `test/smoke_test/syntax/test3_2_smoke.cpp` 覆盖
  `for` 与 `while` 的相互嵌套
- `test/m/test3/test3_3.m` 和 `test/smoke_test/syntax/test3_3_smoke.cpp` 覆盖
  `for / while` 相互嵌套中的 `break / continue` 目标选择

测试重点：

- 生成 `entry + while.header / while.body / while.latch / while.end`
- 条件表达式在 `while.header` 中求值
- `while.body` 普通 fallthrough 跳到 `while.latch`
- `while.latch` 回跳 `while.header`
- `while.end` 接后续 continuation 或隐式 `ret`
- `continue` 生成用户级跳转到 `while.latch`
- `break` 生成用户级跳转到 `while.end`
- `for` 内嵌 `while` 时，内层 `while.end` 回到外层 `for.latch`
- `while` 内嵌 `for` 时，内层 `for.end` 回到外层 `while.latch`
- 混合嵌套中 `break / continue` 始终选择最近一层循环的目标块
