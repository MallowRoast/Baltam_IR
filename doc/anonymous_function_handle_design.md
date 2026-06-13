# 匿名函数句柄设计

本文只记录匿名函数 `@(args) expr` 特有的 IR 语义。字段级 schema 以
[ir_schema.md](./ir_schema.md) 为准；lowering 主流程以
[ir_lowering_design.md](./ir_lowering_design.md) 为准。

## 设计目标

匿名函数句柄需要分清三层语义：

- 函数体代码：`@(x) x + y` 中的 `x + y`
- 构造点捕获：运行到 `f = @(x) x + y` 时捕获当前 `y` 的运行时值
- 调用点分派：`f(1)` 先读取变量 `f` 的运行时值，再由 `value_apply` 分派

这三层不应复用普通名字 lookup，也不应让匿名函数体进入 Matlab 函数名字表。

## 当前 IR 形状

当前实现采用：

- 每个匿名函数体分配一个 module 级 `AnonymousFunctionId`
- 匿名函数体用 `AnonymousFunctionUnit` 表达，继承 `CodeUnit`
- `IRModule::anonymous_functions` 拥有所有匿名函数体
- `AnonymousFunctionUnit::lexical_parent` 指向出现该匿名函数表达式的外层 `CodeUnit`
- 匿名函数没有显式返回参数，也没有 `return_slots`
- 匿名函数体直接 `ret` 表达式 lowering 后的 `ValueId`
- 捕获变量在匿名函数体内使用只读 `Capture` slot 表达
- 外层 IR 通过 `CreateAnonymousFunctionHandleInst(function_id, captures)` 间接引用匿名函数体

这里的 module 级唯一只表示一次 IR 构建会话内唯一，不是进程全局状态。

## 捕获语义

匿名函数捕获的是构造点的运行时值快照：

```matlab
y = 1;
f = @(x) x + y;
y = 10;
z = f(2);
```

`f` 中看到的 `y` 是构造 `f` 时的值。IR 因此分两步表达捕获：

1. 外层构造点先读取自由变量，得到外层 `ValueId`
2. `create_anon_func` 把这个 `ValueId` 对应的运行时值放入 closure capture 环境

匿名函数体不能直接引用外层 `ValueId`，因为 `ValueId` 只在所属 `CodeUnit` 内有效。跨过
匿名函数边界的是 runtime value，不是 IR value。

完整链路是：

```text
外层名字
  -> 外层 load
  -> 外层 ValueId
  -> create_anon_func 捕获运行时值
  -> closure capture value
  -> 调用时填入匿名函数 frame 的 Capture slot
  -> body 内 load
  -> body-local ValueId
```

## 函数与脚本捕获来源

函数、匿名函数和脚本中的静态变量都已经绑定到 slot：

```ir
%1 = load %slot_y
%2 = create_anon_func #anon0 captures { %slot_y }
```

此时 `CaptureValue::source_slot` 是被捕获变量自己的静态 slot。若捕获来源在脚本中，该 slot
的 tag 是 `ScriptVar`；若捕获来源在函数中，则通常是 `Local`、`Arg` 或 `Ret`。
真正被 closure 捕获的是构造点 `load` 得到的运行时值。

## 调用语义

源码里的：

```matlab
z = f(2);
```

在函数中 lower 为：

```ir
%f = load %slot_f
%arg = const 2
%z = value_apply(%f, %arg)
```

`value_apply` 不静态假设 `%f` 一定是匿名函数句柄。运行时再根据 `%f` 的实际值分派：

- 匿名函数句柄：取出 closure code 和 captures 调用
- 具名函数句柄：按句柄内绑定或 unresolved 名字规则调用
- 数组或对象：按索引 / overload 规则继续分派

## 与具名函数句柄的区别

`@sin` 保存名字，并可能在构造时 prebind 到静态目标。

`@(x) x + y` 没有 Matlab 名字空间里的函数名。它构造的是 closure 实例，实例携带：

- 匿名函数体代码引用
- 构造点捕获值

因此匿名函数不能复用 `CreateNamedFunctionHandleInst`。

## 后续优化边界

语义 IR 中保留 capture slot 的 `load`，是为了表达 body-local 数据来源。这不要求最终
执行时一定保留昂贵的 slot 读取。

后续可以在语义保持不变的前提下做：

- capture slot layout：把 capture slot 降成 closure env 的固定 index / offset
- entry hoisting：只读 capture slot 在 entry 读一次后复用
- load forwarding / CSE：合并同一 capture slot 的重复读取
- capture-to-SSA：执行层把 capture slot 转成隐式参数或 SSA 输入
- specialized closure call：证明调用目标后，把 captures 作为已知值传入并继续内联

`store + load` forwarding 和保守 DSE 应作为通用 slot canonicalization pass，而不是
塞进匿名函数 lowering。

## 待定问题

- 自由变量分析继续放在 lowering 内，还是抽成独立 AST 分析 pass
- 捕获值的 runtime 复制语义：深拷贝、COW value，还是沿用当前 `ba_obj` 复制规则
- 匿名函数体引用 local / private / import 时，runtime closure code 如何保存依赖
- closure code object 的缓存、序列化和释放策略
- 逃逸分析如何证明某些匿名函数体可以放入函数局部表，而不是 module 级匿名函数表
