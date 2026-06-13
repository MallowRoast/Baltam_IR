# M 函数工作区中的静态 slot 与动态 env

这份笔记整理一次关于 `slot`、`workspace/env`、`eval` 和脚本调用语义的讨论。核心结论是：

> M 函数的工作区不应只理解成静态 `slot` frame。它还需要一层动态 `env` 属性，用来承载 `eval`、脚本、`assignin`、`load` 等机制在运行时创建的名字绑定。

但这层动态 `env` 不应该反向改变函数体已经静态确定的变量/函数分派。

## 1. 函数工作区的两层状态

M 函数运行时可以拆成两类名字存储：

```text
Function workspace
  static slot frame:
    - arg
    - ret
    - 函数文本中静态确定的 local 变量
    - lowering/runtime 内部 slot

  dynamic env table:
    - eval / evalin / assignin / load / script 运行时创建的名字
    - debugger / who / whos / exist / save 等动态环境观察点可见的名字
```

`slot` 是编译期或 IR 特化期确定的 frame layout 项。它不应该在函数执行过程中因为某个新名字出现而临时扩展。

`env` 是运行时按名字访问的动态表。它可以保存不属于静态 slot frame 的变量绑定。

## 2. `eval('sin = 1')` 的语义

以函数中执行：

```matlab
eval('sin = 1')
```

为例，`sin` 的处理取决于它是否已经在当前函数的静态 slot 表中。

如果 `sin` 已经是当前函数的静态局部变量：

```matlab
function y = f()
  sin = 0;
  eval('sin = 1');
  y = sin;
end
```

那么 `eval` 写入 `sin` 时应更新已有的 `sin` slot。这里的动态环境写入可以解析到静态 slot：

```text
env_store("sin", 1)
  -> found static slot sin
  -> store sin, 1
```

如果 `sin` 不在当前函数的静态 slot 表中：

```matlab
function y = f()
  eval('sin = 1');
  y = sin;
end
```

那么 `eval` 仍然可能在当前函数工作区的动态 env table 中创建名为 `sin` 的变量。但这个新建变量不应改变函数文本里 `y = sin` 对 `sin` 的静态分类。

也就是说，`y = sin` 在 lowering/静态名字解析阶段如果已经被判定为函数名解析或动态 direct call，就不应该因为前面的 `eval` 运行时创建了 `sin` 变量而反向变成 `load sin`。

## 3. 脚本调用也是同一类问题

脚本有自己的静态 `ScriptVar` slot layout，但没有独立的运行时工作区。函数调用脚本时，
脚本 slot 会在运行时绑定到 caller 的函数工作区存储或动态 env 存储。

例如：

```matlab
function y = f()
  myscript;   % myscript.m 内部有：b = 1
  y = b;
end
```

执行 `myscript` 时，脚本中的 `b` 对应脚本自己的 `ScriptVar` slot。若 caller `f` 中没有
静态 slot `b`，这个脚本 slot 可以绑定到 `f` 的动态 env 中的 `b` 存储。但是，如果 `b`
不是 `f` 函数文本中静态确定的变量名，那么 `f` 里的：

```matlab
y = b;
```

不应该读这个动态 env 变量。它在静态名字解析时已经不是 local variable，而应继续走函数名解析、零参数调用或未定义函数错误等路径。

如果希望脚本写入的是 `f` 的静态 local slot，需要在 `f` 的函数文本中显式让该名字成为变量：

```matlab
function y = f()
  b = [];
  myscript;   % myscript.m 内部有：b = 1
  y = b;
end
```

这里 `b` 已经属于 `f` 的静态 slot 表。脚本中的 `ScriptVar b` 可以在运行时绑定到这个
caller slot，后续 `y = b` 也可以 lower 成 `load b`。

## 4. 动态 env 可以被动态语义观察

虽然 `eval` 或脚本创建的动态 env 变量不会改变函数体的静态分派，但它们并不一定是无意义状态。

这些变量可能被以下机制观察或使用：

- 后续 `eval`
- 后续脚本调用
- `evalin`
- `assignin`
- `who`
- `whos`
- `exist`
- `save`
- debugger / 交互式观察

因此，动态 env 写入不能简单地一律删除。只有在能够证明后续没有任何 env observer 或动态代码执行点时，才可以把这些动态 env 变量当成死状态删除。

例如：

```matlab
function y = f()
  myscript;   % 动态创建 b
  y = 1;
end
```

如果 `myscript` 创建的 `b` 后续既不会被 `eval`、脚本、`who/whos/exist/save`、debugger 等观察，也不会影响错误行为，那么非调试模式下可以考虑把这个动态 env 写入作为死写删除。

## 5. 动态 env 不影响 M 函数内部的静态分派

函数内部的普通语句应基于函数文本建立静态名字表。一个名字是否是变量，主要由以下来源决定：

- 参数
- 返回值
- 普通赋值左值
- `for` / `catch` 等语法绑定
- `global` / `persistent` 声明
- 其他函数文本中明确的静态变量声明机制

`eval`、脚本、`assignin`、`load` 等动态机制创建的名字，不应自动加入当前 `FunctionUnit` 的静态 slot 表，也不应改变已经 lower 好的调用分派。

因此：

```matlab
function y = f()
  myscript;   % 动态创建 b
  y = b;
end
```

中的 `b` 不应因为 `myscript` 创建了动态 env 变量而变成 `load b`。

这个边界可以避免一个关键错误：运行时环境里的动态名字反向污染函数体的静态语义。

## 6. `ScriptVar` 什么时候可以直接绑定到 caller slot

脚本自身的基础 IR 会为静态出现的变量创建 `ScriptVar` slot：

```text
%slot_x = script @x
load %slot_x
store %slot_x, value
```

因为脚本可以在不同 runtime workspace 中运行，`ScriptVar` slot 的实际存储位置不能只靠脚本
自身决定。

当脚本在某个已知函数上下文中执行时，可以做上下文特化：

```text
script SlotTable + caller function frame layout -> runtime slot binding
```

如果 `x` 已经是 caller 的静态 slot，脚本的 `ScriptVar x` 可以直接绑定到 caller 的
`Local/Arg/Ret` slot 存储。此后脚本内：

```text
load %script_slot_x
store %script_slot_x, value
```

执行时等价于访问 caller 的同一份存储：

```text
load caller.x
store caller.x
```

但这种绑定只适用于已经存在于 caller 静态 slot 表中的名字。它不应因为脚本中出现了新的
赋值目标，就为 caller 临时创建新的静态 slot。

也就是说：

```matlab
function f()
  a = 1;
  myscript;  % myscript 读写 a
end
```

`myscript` 中的 `ScriptVar a` 可以在 `f` 上下文中绑定为 caller slot 访问。

而：

```matlab
function f()
  myscript;  % myscript 创建 b
end
```

如果 `b` 不在 `f` 的静态 slot 表中，`myscript` 中的 `ScriptVar b` 应绑定到动态 env
table，而不是创建 `f.b` slot。

## 7. 特化阶段

脚本 slot 到具体存储位置的绑定不属于基础 lowering。更合适的阶段是脚本执行前或调用目标
解析后的上下文特化：

```text
1. 基础 lowering
   script 名字统一进入 ScriptVar slot，并 lower 成 load / store
   function 名字按静态变量表 lower 成 load / store 或调用分派

2. 静态特化
   如果编译期已经知道脚本目标和 caller frame layout，
   可以提前生成或缓存 specialized script slot binding。

3. 运行期 / JIT 特化
   如果脚本目标只能运行期解析，
   则在解析到具体脚本和 caller frame layout 后，
   生成或查找缓存的 slot binding。
```

特化缓存可以按以下信息失效：

```text
script source version
caller function frame layout id
path/world version
debug/env-observer mode
```

每个运行时脚本 slot 还需要一个绑定是否失效的状态。`clear x`、`eval`、`assignin` 或其他会
改变名字存储关系的操作可以让绑定失效；后续访问该 slot 时再走 lookup，必要时重新绑定。

## 8. IR 设计结论

当前 IR 可以保持以下分层：

- `SlotTable`：保存源码中静态出现的变量和 lowering 内部状态。
- `Slot { id, tag }`：表达变量的静态编号和静态类别；是否有效只由 `SlotId` 决定。
- `ScriptVar` slot：脚本变量的静态身份；运行时绑定到 base/caller 的静态 slot 或动态 env
  存储。
- `LoadSlotInst / StoreSlotInst`：表达对 slot 的读写，文本 IR 打印为 `load` / `store`。
- `eval` / `script` / `assignin` / `load` 等：具有 `Env` effect，可能让脚本 slot 绑定失效，
  或读写动态 env。

最重要的约束是：

> 动态 env 可以存在于 M 函数工作区中，但它只服务动态语言机制和环境观察点；它不应反向改变 M 函数普通语句已经静态确定的变量/函数分派。
