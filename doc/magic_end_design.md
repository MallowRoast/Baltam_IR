# Magic End Lowering 设计

## 1. 目标

`end` 在 Matlab 语义中不是普通名字，也不是普通函数调用。它只在索引表达式上下文中有意义，
例如 `A(end)`、`A(1:end)`、`A(end, 1)`。

基础 IR lowering 的目标是保留这个特殊语义，而不是把它提前改写为某个 runtime helper：

- 所有 `node_magic_end` 都 lower 成 `MagicEndInst`
- `MagicEndInst` 记录 `end` 可能归属的索引上下文、当前维度和总索引数
- 后续 pass / runtime 再选择合法索引上下文，并实现普通数组和类对象 `end` 方法语义
- 用户路径上的普通 `end.m` 不参与名字解析

这样 IR 不需要假设解释器当前已经正确处理类对象索引中的 `end` 方法。基础 lowering 只生成
显式、可验证、可后续降级的 magic-end 节点。

## 2. 语义边界

`end` 的查找规则与普通函数名不同：

- 独立表达式 `end` 不合法，基础 lowering 不会把它当作 `load @end` 或 `call @end`
- 用户路径上的 `end.m` 不是合法候选
- 类对象索引中的自定义 `end` 方法有效，但它属于索引协议的一部分
- 类 `end` 方法不应在基础 lowering 阶段表现为普通 direct call

因此 `MagicEndInst` 是 magic `end` 的 canonical high-level IR。后续阶段可以把它收敛为
专用 runtime helper、专用 bytecode，或更低层的内部调用，但那是 `MagicEndInst` 的 lowering /
execution 策略，不是 AST 到 IR 的基础表示。

## 3. MagicEndInst

`MagicEndInst` 的构建期结果类型保持 `unknown`，并携带候选上下文链。普通数组索引下
`end` 通常会得到整数索引，但类对象可以自定义 `end` 方法，基础 IR 不能提前假设返回类型：

```text
MagicEndContext
  callee_or_base : Operand
  dim            : uint32
  nindices       : uint32
```

候选上下文按内层到外层排列。单层索引只有一个候选：

```matlab
single_value = A(end);
```

脚本 lowering 保留为：

```ir
[%0, unknown] = magic_end([(A, 1, 1)])
[%1, unknown] = apply @A(%0)
```

函数中 `A` 已经是运行时值，但仍使用同一个节点形状：

```ir
[%0, unknown] = load %arg0
[%1, unknown] = magic_end([(%0, 1, 1)])
[%2, unknown] = value_apply(%0, %1)
```

这里的 `%0` 只说明 `end` 出现在以 `%0` 为 base 的索引上下文中，不说明基础 lowering 已经完成
数组或类对象的 `end` 语义。文本 IR 把每个候选显示为 `(base, dim, nindices)`，
方便和具体索引层对照。

## 4. 候选 Operand 语义

`MagicEndContext::callee_or_base` 的 Operand 种类是语义信息，不能全部当作普通名字重新查找：

| Operand 种类 | 语义 |
| --- | --- |
| `InternedString` | 尚未解析的源码名字。后续名字解析或运行时分派可以判断它是变量索引还是函数调用。若解析为普通函数调用，该候选不是索引上下文，`end` 可继续尝试外层候选。 |
| `Slot` | 已经绑定到当前函数 / workspace 的变量 slot。执行时应检查 slot binding state；cleared / unbound 是变量引用错误，不回退到函数名解析。 |
| `ValueId` | 已经加载或计算出的 runtime base 值。它表示候选已经绑定到具体数据流，后续只判断该值是否支持索引 `end` 语义。 |

因此函数中的静态变量名一旦形成 slot，`clear` 不会让同名函数重新参与这次 `end` 归属判断。
例如：

```matlab
function y = f()
A = 10:10:50;
sin = [2 4];
clear sin;
y = A(sin(end));
end
```

这里 `sin(end)` 的 `end` 候选仍应锚定到 `sin` 的变量 slot。执行到该候选时发现 slot 已
cleared，应报类似 `Reference to a cleared variable sin` 的错误，而不是把 `sin(end)` 改按
builtin `sin` 调用并让 `end` 回退到外层 `A(...)`。

相对地，如果某个名字从未形成变量 slot，只是 unresolved name，那么它仍可在后续解析为函数
调用；此时该候选不是索引上下文，`end` 才能继续尝试外层候选。

## 5. 多层候选

典型例子：

```matlab
nested_value = A(fun(end));
```

当 `fun(end)` 仍可能是 unresolved apply 时，基础 lowering 不能仅靠 AST 判断：

- `fun(end)` 是否是对变量 `fun` 的索引
- `fun(end)` 是否是普通函数调用
- `end` 是否应继续归属外层 `A(...)`

因此 IR 保留候选链：

```ir
[%6, unknown] = magic_end([(fun, 1, 1) -> (A, 1, 1)])
[%5, unknown] = apply @fun(%6)
[%4, unknown] = apply @A(%5)
```

函数中的嵌套 `value_apply` 也可以保留同样的候选链，只是候选 base 通常是 `ValueId`：

```ir
[%1, unknown] = load %arg0
[%2, unknown] = load %arg1
[%3, unknown] = magic_end([(%2, 1, 1) -> (%1, 1, 1)])
[%4, unknown] = value_apply(%2, %3)
[%5, unknown] = value_apply(%1, %4)
```

后续解析规则应按内层到外层处理候选。未解析名字候选如果解析为普通函数调用，可以继续尝试
外层；已绑定 `Slot` / `ValueId` 候选如果遇到 cleared / unbound slot 或不支持索引 `end`
语义，应直接按该候选报错，而不是当作候选失败继续外层。只有所有可跳过的未解析名字候选都
不提供索引上下文时，该 `end` 才按无合法索引上下文报错。

## 6. 后续降级

`internal.end_index(base, dim, nindices)` 可以作为后续实现策略之一，但不由基础 lowering 直接生成。
如果后续 pass 选择这种形式，它必须保证：

- `base` 已经被解析为合法索引上下文
- 普通数组按维度大小计算 `end`
- 类对象按索引协议调用对应的 `end(obj, dim, nindices)` 语义
- 非索引上下文或不支持索引 `end` 的值报错

也可以选择生成专用 bytecode 或保留 `MagicEndInst` 到 interpreter 执行层。关键约束是：类对象
`end` 方法的正确性由处理 `MagicEndInst` 的阶段负责，不能假设普通函数调用路径已经覆盖。

## 7. 当前测试覆盖

- `test8`
  - 函数内 `A(end)`
  - `A(end - 1)`
  - `A(1:end)`
  - 二维 `A(end, 1)` / `A(1, end)`
  - `A(fun(end))`
  - `A(end) = ...`
- `test8_1`
  - 脚本单层 `A(end)` 生成单候选 `MagicEndInst`
  - 脚本多层 `A(fun(end))` 生成 `fun -> A` 候选链 `MagicEndInst`
