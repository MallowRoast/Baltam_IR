# Baltam_IR `A(...)` / `A.a` / `A{...}` 设计

## 目标

本文描述三组语法在 `Baltam_IR` 中的设计：

- `A(...)` 与 `A(...) = rhs`
- `A.a` 与 `A.a = rhs`
- `A{...}` 与 `A{...} = rhs`

重点说明：

- 它们在 AST 中的真实形状
- lowering 的 canonical 规则
- non-SSA / SSA 中如何表示
- 解释器在运行期如何执行

本文中的 AST 形状已经用最小脚本实测核对。

## 总结表

| 语法 | AST 主类型 | lowering 结果 | 设计要点 |
| --- | --- | --- | --- |
| `x = A()` | `node_multiple_func` | `CallNode(Direct/Indirect)` | 不能在 lowering 阶段简单断言“函数调用”或“圆括号取值” |
| `A() = rhs` | `node_asgn(lhs=node_multiple_func)` | `call @__ir_paren_set__` | setter 语义明确，直接 canonicalize |
| `y = A.a` | `node_asgn(rhs=node_struct_get)` | `call @getfield` | dot 语义已在 AST 层消歧 |
| `A.a = rhs` | `node_struct_set` | `call @setfield` + store-back | 需要沿左值链递归回写 |
| `z = A{1}` | `node_asgn(rhs=node_cell_get)` | `call @__ir_cell_get__` | brace 语义已在 AST 层消歧 |
| `A{1} = rhs` | `node_cell_set` | `call @__ir_cell_set__` | setter 语义明确，直接 canonicalize |

下文统一记作：

- `A(...)`
- `A.a`
- `A{...}`

其中 `A{...}` 的 probe 用的是 `A{1}`，`A(...)` 的 probe 用的是 `A()`。

## 一、`A(...)`

## AST 形状

表达式 / 调用形状统一使用 `node_multiple_func`。

实际 probe：

```text
x = A()
```

对应 AST：

```text
node_multiple_func
  branch[0] = out_args
  branch[1] = s() / callee-or-base
  branch[2] = in_args
```

probe 里 `x = A()` 并不是：

```text
node_asgn(node_name(x), node_multiple_func(...))
```

而是直接一个带 `out_args` 的 `node_multiple_func`。

这点和 `A.a`、`A{...}` 很不一样。

## 为什么不能简单理解成“函数调用”或“取下标”

`A(...)` 在语法层保留二义性：

- 如果 `A` 是函数或函数句柄，它是调用
- 如果 `A` 是矩阵、结构体数组、对象等值，它可能是圆括号取值

因此 lowering 不能一上来把它固定成：

- `call @foo(...)`
- 或 `call @__ir_paren_get__(...)`

当前设计选择保留这个二义性，直到运行期再决策。

## lowering 逻辑

表达式位置的 `node_multiple_func` 会 lower 成普通 `CallNode`：

- `Direct`：当 callee 在当前语境里被视为已知函数名
- `Indirect`：当 callee 应被视为“一个值”

这里没有单独的 `ParenGetNode`。

也就是说，`A(...)` 的 IR 仍然是“调用形状”，但这个调用可能在运行期退化成圆括号取值。

## non-SSA IR

```text
%out = call @foo(%args...)          ; direct
%out = call %A(%args...)            ; indirect
```

`A(...)` 是否是函数调用，non-SSA 不在节点类型上区分，只在 callee 类型上区分。

## untyped SSA

non-SSA `CallNode` 会直接转成 `SSACallNode`：

- direct call -> `SSACallNode::Callee::Direct`
- indirect call -> `SSACallNode::Callee::Indirect`

这里同样没有额外的 paren-get SSA 节点。

## 解释器运行期

解释器执行 indirect call 时会看 callee 的运行时值：

- 如果是 `function_handle`，按函数调用执行
- 否则，把它解释成圆括号取值，走 runtime `block`

也就是说，真正的“是调用还是取值”是在运行期通过值类型决定的。

## 二、`A(...) = rhs`

## AST 形状

`A(...) = rhs` 的 AST 不是 `node_multiple_func`，而是：

```text
node_asgn
  branch[0] = node_multiple_func
  branch[1] = rhs
```

probe 结果：

```text
node_asgn
  node_multiple_func branches=3 callee=A
    <null>
    node_name A
    node_list ...
  node_name rhs
```

也就是说：

- lhs 是 `node_multiple_func`
- rhs 是普通 rhs 表达式

## lowering 逻辑

当前 lowering 把它识别成圆括号写回，而不是普通赋值。

canonical 形式是直接转成 helper call：

```text
%A.next = call @__ir_paren_set__(%A.cur, %idx..., %rhs)
```

这里的设计意图是：

- `A(...) = rhs` 已经没有“函数调用 vs 取值”的二义性
- 在赋值语境里，它只可能是圆括号写回

因此可以在 lowering 阶段唯一化。

## non-SSA IR

non-SSA 层没有单独的 `ParenSetNode`，而是用 direct helper call：

```text
call @__ir_paren_set__(base, idx..., value)
```

返回值是“更新后的 base”。

## untyped SSA

SSA 后仍然是普通 `SSACallNode`，callee 为 direct：

```text
%A.next = call @__ir_paren_set__(%A.cur, %idx..., %rhs)
```

## 解释器运行期

解释器把 `__ir_paren_set__` 分派到 runtime `block set`。

需要注意的是，runtime `block set` 会原地修改 base，所以解释器会先复制一份 base，再把复制体传进去。这样才能满足 SSA 的“每次写回产生新版本值”。

## 当前限制

当前实现还没有支持：

```matlab
global A;
A(...) = rhs;
```

也就是 global 根对象的圆括号写回仍未接通。

## 三、`A.a`

## AST 形状

`A.a` 在 AST 层已经被 parser 消歧成 `node_struct_get`。

probe 结果：

```text
y = A.a
```

对应：

```text
node_asgn
  branch[0] = node_name(y)
  branch[1] = node_struct_get
    branch[0] = node_name(A)
    branch[1] = node_name(a)
```

所以 `A.a` 不再经过 `node_multiple_func`。

## lowering 逻辑

`node_struct_get` 直接 lower 成：

```text
%out = call @getfield(%base, %field)
```

字段选择器有两种情况：

- 静态字段：`A.a`
- 动态字段：`A.(name_expr)`

当前 lowering 对静态字段会直接造一个 `const.text "a"`，动态字段则把字段表达式本身继续 lower 成普通操作数。

## non-SSA IR

使用普通 direct `CallNode`：

```text
%field = const.text "a"
%out = call @getfield(%A, %field)
```

没有单独的 `StructGetNode`。

## untyped SSA

直接转成 `SSACallNode`：

```text
%out = call @getfield(%A, %field)
```

## 解释器运行期

解释器不需要为 `A.a` 增加专门 helper；它把 `getfield` 当作普通 builtin 调用。

这也是 dot 访问和 paren 访问设计上的重要区别：

- `A(...)` 需要保留二义性
- `A.a` 在 AST 层已经完全消歧

## 四、`A.a = rhs`

## AST 形状

`A.a = rhs` 不是 `node_asgn(node_struct_get, rhs)`，而是直接：

```text
node_struct_set
  branch[0] = base
  branch[1] = field
  branch[2] = rhs
```

最简单的 probe 结果：

```text
node_struct_set
  node_name A
  node_name a
  node_name rhs
```

对于嵌套形式：

```matlab
A.a.b = rhs
```

`branch[0]` 可以继续是 `node_struct_get(...)`。所以 `node_struct_set` 的 base 不一定永远是裸名字。

## lowering 逻辑

当前实现把它拆成两步：

1. 先生成更新后的 base 值
2. 再把这个更新后的 base 沿左值链写回

第一步：

```text
%base.cur = ...
%field = ...
%base.next = call @setfield(%base.cur, %field, %rhs)
```

第二步：

- 如果 base 是 `A`，写回到 `A`
- 如果 base 是 `A.a`，先把新的 `A.a` 写回到 `A`
- 如果根是 global，则最终落到 `global.store`

这就是为什么当前实现保留 lhs base 的 AST，而不是先把它 lower 成单个值：store-back 还需要知道完整左值链形状。

## non-SSA IR

当前没有单独的 `StructSetNode`，而是：

- 一个 `call @setfield(...)`
- 加若干 assign / global.store 组成的回写链

例如：

```text
%t0 = call @setfield(%A, %"a", %rhs)
%A = %t0
```

如果根对象是 global：

```text
%t0 = global.load @A
%t1 = call @setfield(%t0, %"a", %rhs)
global.store @A, %t1
```

## untyped SSA

SSA 后仍然是普通 direct call 加普通值回写，没有额外的 struct-set SSA 节点。

也就是说，dot setter 的“特殊性”主要在 lowering，而不在最终 IR 节点种类。

## 解释器运行期

和 `A.a` 一样，解释器把 `setfield` 当作普通 builtin 调用执行，不需要额外 helper。

## 五、`A{...}`

## AST 形状

`A{...}` 在 AST 层已经被 parser 消歧成 `node_cell_get`。

probe 用的是：

```matlab
z = A{1}
```

得到：

```text
node_asgn
  branch[0] = node_name(z)
  branch[1] = node_cell_get
    branch[0] = node_name(A)
    branch[1] = node_list(indices...)
```

所以 brace get 和 dot get 一样，也不会经过 `node_multiple_func`。

## lowering 逻辑

`node_cell_get` 直接 lower 成 helper：

```text
%out = call @__ir_cell_get__(%base, %idx...)
```

这里没有保留任何“函数调用 vs 取值”的二义性，因为 AST 已经把 brace 语义固定下来了。

## non-SSA IR

使用 direct `CallNode`：

```text
%out = call @__ir_cell_get__(%A, %i, %j, ...)
```

## untyped SSA

直接转成 `SSACallNode`：

```text
%out = call @__ir_cell_get__(...)
```

## 解释器运行期

解释器把 `__ir_cell_get__` 分派到 runtime `brace_get`。

这里也没有 `A(...)` 那种运行期二次消歧过程。

## 六、`A{...} = rhs`

## AST 形状

brace setter 直接是：

```text
node_cell_set
  branch[0] = base
  branch[1] = indices
  branch[2] = rhs
```

probe：

```text
node_cell_set
  node_name A
  node_list(...)
  node_name rhs
```

## lowering 逻辑

当前实现直接 canonicalize 成：

```text
%A.next = call @__ir_cell_set__(%A.cur, %idx..., %rhs)
```

这和 `A(...) = rhs` 类似，setter 语义在 lowering 阶段已经完全确定。

## non-SSA IR

使用 direct helper call：

```text
call @__ir_cell_set__(base, idx..., value)
```

返回值是新的 base。

## untyped SSA

仍然是普通 `SSACallNode`。

## 解释器运行期

解释器把 `__ir_cell_set__` 分派到 runtime `brace_set`。

和 paren setter 一样，解释器会先复制 base，再执行 runtime 写回，从而保持 SSA 的“新版本值”语义。

## 当前限制

当前实现还没有支持：

```matlab
global A;
A{...} = rhs;
```

global 根对象的 brace 写回仍未接通。

## 七、为什么三类语法要分开设计

核心原因是 parser 消歧程度不同。

### `A(...)`

语法层仍然保留二义性：

- 可能是调用
- 可能是圆括号取值

因此表达式读取路径必须把这件事拖到运行期。

### `A.a`

语法层已经明确是 dot 访问，因此 lowering 可以直接生成 `getfield/setfield`。

### `A{...}`

语法层已经明确是 brace 访问，因此 lowering 可以直接生成 `__ir_cell_get__/__ir_cell_set__`。

换句话说：

- `A(...)` 的核心问题是“读取语义需要延迟判定”
- `A.a` / `A{...}` 的核心问题是“写回时如何保持值语义和 store-back 语义”
