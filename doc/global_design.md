# Baltam_IR `global` 设计

## 目标

本文说明 `global` 在 `Baltam_IR` 中的表示与执行方式，覆盖：

- lowering 侧如何识别和传播 `global`
- non-SSA IR 里的具体节点
- untyped SSA 转化后的对应节点
- 解释执行时的运行期结构

本文描述的是当前实现，而不是未来规划中的另一套方案。

## 语义边界

`global` 在当前实现里被建模为“函数外部的共享工作区状态”：

- 它不是局部 SSA 名字
- 它不会直接参与 phi 合并
- 它通过显式 `load/store` 节点进入 IR

这点和普通局部变量不同。局部变量在 non-SSA 阶段还是“具名可重定义值”，进入 SSA 后会被重命名；`global` 从一开始就被当作外部状态槽位。

## lowering 设计

### 1. 函数级预收集

lowering 在进入函数前会先收集两类名字：

- `user_names`
- `global_names`

这样做的目的不是为了生成 IR，而是为了让 lowering 从函数入口开始就知道：

- 哪些名字是局部具名值
- 哪些名字应当按 `global` 处理

当前实现把 `global` 视为函数级属性，而不是“从某条语句之后才生效”的局部控制流事件。

### 2. `node_global` 自身不生成运行时 IR

AST 里的 `node_global` 只负责声明：

- 这些名字属于 `global`

它不会直接 lower 成运行时指令。当前上下文已经在函数入口带上了整函数的 `global_names`，所以语句级 `node_global` 主要起语义对齐和断言作用。

### 3. 读 `global`

当表达式位置出现名字 `A`，且 `A` 是 active global name 时，lower 成：

```text
%t = global.load @A
```

也就是 non-SSA 的 `GlobalLoadNode`。

### 4. 写 `global`

当出现：

```matlab
A = rhs;
```

且 `A` 是 active global name 时，先 lower `rhs`，再发：

```text
global.store @A, %rhs
```

也就是 non-SSA 的 `GlobalStoreNode`。

### 5. 结构体字段写回

对于：

```matlab
A.a = rhs;
```

当前实现不会引入单独的“struct write-back IR 节点”，而是展开成：

1. 先读取 base 的当前值
2. 调 `setfield(base, field, value)` 生成更新后的对象
3. 再沿左值链把更新后的对象写回根对象

如果根对象是 `global`，最后一步会落到：

```text
global.store @A, %updated
```

也就是说，`global struct` 的支持不是通过额外的 global-struct 节点完成的，而是通过：

- 普通 `setfield`
- 加上根位置的 `GlobalStoreNode`

### 6. `global` 上的 `A(...)` / `A{...}` 访问

对于表达式位置的：

```matlab
A(...)
```

如果根名字 `A` 是 global，lowering 不会试图从本地名字表里找 `%A`，而是会先显式读取：

```text
%t0 = global.load @A
%t1 = call %t0(%idx...)
```

这里 `%t0(%idx...)` 仍然是 indirect call 形状；真正“这是函数调用还是圆括号取值”仍然交给运行期决定。

对于写回位置的：

```matlab
A(...) = rhs
A{...} = rhs
```

当前实现都会走统一的 load-modify-store 展开：

```text
%t0 = global.load @A
%t1 = call @__ir_paren_set__(%t0, %idx..., %rhs)
global.store @A, %t1
```

或：

```text
%t0 = global.load @A
%t1 = call @__ir_cell_set__(%t0, %idx..., %rhs)
global.store @A, %t1
```

也就是说，global 的索引/元胞写回和 struct 写回一样，本质上都是“先得到更新后的整个 base，再把整个 base 写回 global 槽位”。

### 7. call 输出写回 `global`

对于：

```matlab
A = foo(...);
```

如果 `A` 是 global，call 输出不能直接绑定到本地 `%A`。当前实现会：

1. 先让 call 输出落到临时值
2. 再把临时值 store-back 到 `global`

这样可以统一处理：

- `A = foo(...)`
- `[A, x] = foo(...)`
- `[A(...), x] = foo(...)`

## non-SSA IR 设计

`global` 在 non-SSA 层只有两个专用节点。

### `GlobalLoadNode`

语义：

```text
%result = global.load @symbol
```

字段：

- `result`
- `symbol`

它表示“从共享 global 工作区读取一个值，产生一个普通 IR 值结果”。

### `GlobalStoreNode`

语义：

```text
global.store @symbol, %value
```

字段：

- `symbol`
- `value`

它表示“把一个普通 IR 值写回共享 global 工作区”。

### 为什么不用 helper call

`global` 没有被设计成：

```text
call @__ir_global_get__(...)
call @__ir_global_set__(...)
```

原因是这里更适合显式建模外部状态访问。这样做有几个好处：

- IR 可读性更高
- verifier / def-use 更容易区分“本地值”和“外部状态访问”
- SSA 构造时不会误把 `global` 当作局部名字版本链的一部分

## untyped SSA 设计

non-SSA 到 SSA 的转化是一对一的：

- `GlobalLoadNode` -> `SSAGlobalLoadNode`
- `GlobalStoreNode` -> `SSAGlobalStoreNode`

### `SSAGlobalLoadNode`

语义：

```text
%v = global.load @A
```

它会定义一个新的 SSA value id。

### `SSAGlobalStoreNode`

语义：

```text
global.store @A, %v
```

它消费一个 SSA value ref，但不会为 `A` 自己创建 SSA 名字。

### 为什么 `global` 不进 phi

`global` 在当前设计里是外部状态，不是函数内部可重命名的局部名字，因此：

- 不给 `global symbol` 建 phi
- 不把 `global symbol` 压入局部名字栈
- 只把每次 `load` 的结果当普通 SSA 值

换句话说，进入 SSA 的是“load 的结果值”，不是“global 槽位本身”。

## 解释器运行期结构

解释器侧为 `global` 增加了共享工作区：

```cpp
struct RuntimeWorkspace {
    std::unordered_map<std::string, Value::Object> globals;
};
```

执行入口通过：

```cpp
struct ExecutionOptions {
    std::shared_ptr<RuntimeWorkspace> workspace;
};
```

把这个共享工作区传入执行。

### 默认创建

如果调用者没有显式传 `workspace`，解释器会在顶层执行时创建一个新的 `RuntimeWorkspace`。

### 嵌套调用共享

模块内函数互相调用时，会沿用同一个 `workspace`。因此：

- `f1()` 写入的 `global`
- `f2()` 能立即读到

这就是 `test38` 里跨函数共享的语义基础。

### `global.load` 的执行语义

解释器执行 `SSAGlobalLoadNode` 时：

1. 到 `workspace->globals[symbol]` 查找
2. 如果不存在，返回空 `double([])`
3. 如果存在，返回该对象的拷贝

这里返回拷贝而不是直接别名共享，是为了避免 SSA 值和工作区状态在运行期发生意外别名污染。

### `global.store` 的执行语义

解释器执行 `SSAGlobalStoreNode` 时：

1. 取输入 SSA 值对应的具体对象
2. 拷贝一份
3. 写入 `workspace->globals[symbol]`

同样，这里也使用拷贝，避免工作区对象和某个 SSA 值对象句柄直接别名。

## 例子

MATLAB：

```matlab
global A;
A = 1;
y = A + 2;
```

non-SSA 形状：

```text
%t0 = const 1
global.store @A, %t0
%t1 = global.load @A
%t2 = const 2
%y = add %t1, %t2
```

如果是：

```matlab
global S;
S.a = 3;
```

当前设计更接近：

```text
%t0 = const 3
%t1 = global.load @S
%t2 = const.text "a"
%t3 = call @setfield(%t1, %t2, %t0)
global.store @S, %t3
```

## 当前支持边界

当前实现已经支持：

- `global A`
- `A = rhs`
- `rhs` 中读取 `A`
- `rhs` 中读取 `A(...)`
- `rhs` 中读取 `A{...}`
- `A = foo(...)`
- `A(...) = rhs`
- `A{...} = rhs`
- `A.a = rhs`
- `rhs` 中读取 `A.a`
- `[A(...), x] = foo(...)`
- 跨模块内函数共享同一个 `global` 工作区
