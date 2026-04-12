# Baltam_IR 短路逻辑 CFG 设计

## 目标

本文说明 `||` / `&&` 在 `Baltam_IR` 中的 canonical 形式。

结论：

- AST 解释器可以继续直接求值
- non-SSA lowering 和 untyped SSA 都以显式 CFG 为准
- 不把 `||` / `&&` 当作普通 `BinOpNode`
- 不新增专门的 SSA 短路节点

本文只讨论短路逻辑：

- `||`
- `&&`

不讨论按元素逻辑运算：

- `|`
- `&`

后者仍然是普通二元运算。

## 为什么用 CFG

短路逻辑的本质是：

- 先求左操作数
- 根据左操作数的条件真值，决定右操作数是否求值

而普通二元运算节点表达的是：

- 左右操作数都已经求值
- 再做组合

因此 `||` / `&&` 不能 canonicalize 成普通 `BinOpNode`。

AST 解释器之所以可以“直接求值”，是因为它拿到的还是未求值的 AST 子树，可以自然地按：

1. 求 `lhs`
2. 做条件判断
3. 视情况求 `rhs`

的顺序执行。

但一旦进入 IR，尤其是 SSA，短路逻辑更合适的标准形式就是 CFG。

## 分层约定

### AST 层

AST 解释器继续保留 direct eval 语义。

也就是说，AST 层不需要为了和 IR 表示一致而强行改成显式基本块。

### non-SSA IR 层

short-circuit 在 lowering 阶段直接展开成显式控制流：

- 条件跳转
- 若干基本块
- 在 merge 点给目标名字赋布尔结果

这样 non-SSA 和 untyped SSA 的语义边界一致。

### untyped SSA 层

SSA 构造后，短路逻辑自然表现为：

- `br`
- `phi`
- 以及块内的普通表达式节点

这也是后续 `SimplifyCFG`、常量传播、DCE 更容易处理的形式。

## 结果类型

`||` / `&&` 的结果应始终是逻辑值。

因此在 IR 上，merge 点最终得到的值语义上等价于：

- `true`
- `false`

而不是：

- 左操作数原值
- 右操作数原值

这点和 JavaScript / Python 那类“返回操作数本身”的语言不同。

## canonical 形状

以下伪 IR 仅描述目标形状，不要求 value id 或 block 名字完全一致。

### `x = lhs || rhs`

目标形状：

```text
%lhs = ...
br %lhs, label %or.short, label %or.rhs

or.short:
  %x.short = const true
  br label %or.end

or.rhs:
  %rhs = ...
  br %rhs, label %or.rhs.true, label %or.rhs.false

or.rhs.true:
  %x.rhs.true = const true
  br label %or.end

or.rhs.false:
  %x.rhs.false = const false
  br label %or.end

or.end:
  %x = phi [ %x.short, %or.short ],
           [ %x.rhs.true, %or.rhs.true ],
           [ %x.rhs.false, %or.rhs.false ]
```

语义要点：

- `lhs` 为真时，`rhs` 不求值
- `rhs` 只有在需要时才进入

### `x = lhs && rhs`

目标形状：

```text
%lhs = ...
br %lhs, label %and.rhs, label %and.short

and.short:
  %x.short = const false
  br label %and.end

and.rhs:
  %rhs = ...
  br %rhs, label %and.rhs.true, label %and.rhs.false

and.rhs.true:
  %x.rhs.true = const true
  br label %and.end

and.rhs.false:
  %x.rhs.false = const false
  br label %and.end

and.end:
  %x = phi [ %x.short, %and.short ],
           [ %x.rhs.true, %and.rhs.true ],
           [ %x.rhs.false, %and.rhs.false ]
```

语义要点：

- `lhs` 为假时，`rhs` 不求值

## 为什么不直接降成 helper call

例如不采用：

```text
%x = call @__ir_short_or__(%lhs, %rhs)
```

原因是这会丢掉“右侧可能根本不执行”的结构信息。

除非 IR 继续携带 thunk / lazy operand 之类的高阶语义，否则 helper call 只适合表达“两个值已经就绪之后的计算”，不适合表达短路。

## 和 setter 的组合

短路表达式可以出现在：

- `if` / `while` 条件
- 函数入参
- 算术表达式内部
- 赋值右值
- setter 右值

例如：

```matlab
L(2:3) = (numel(a) < 4 || a(4) == 5);
s.a.b = (numel(a) < 4 || a(4) == 5);
c{2} = (numel(a) < 4 || a(4) == 5);
```

这里的 canonical 规则是：

1. 先把短路表达式本身 lower 成 CFG，得到一个普通临时值 `%sc`
2. 再把 `%sc` 作为 setter 的 value 输入

也就是说 setter 层不理解短路语义，setter 只消费已经算好的结果值。

例如：

```text
%sc = ... ; short-circuit CFG result
%L.1 = call @__ir_paren_set__(%L.0, %idx, %sc)
```

或：

```text
%sc = ... ; short-circuit CFG result
%tmp = call @__ir_getfield_for_write__(%s.0, %"a")
%tmp2 = call @setfield(%tmp, %"b", %sc)
%s.1 = call @setfield(%s.0, %"a", %tmp2)
```

如果 setter 的 base 还是未初始化的本地名字，解释器会按 setter 类型补一个空 base：

- `__ir_paren_set__` -> `[]`
- `__ir_cell_set__` -> 空 cell
- `setfield` / `__ir_getfield_for_write__` -> 空 struct

因此短路表达式作为 rhs 时，不需要额外先“显式构造一个空容器”，再进入 setter 路径。

## 当前实现约束

在当前阶段：

- `||` / `&&` 继续在 lowering 阶段展开成 CFG
- `|` / `&` 仍然是普通 `BinOpNode`
- 不引入 `ShortCircuitNode`
- 不引入专门的 SSA 短路节点

如果后续为了 non-SSA 可读性想增加高层短路节点，也应在进入 SSA 前统一展开回本文的 CFG 形式。
