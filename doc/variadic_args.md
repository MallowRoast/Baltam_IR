# MATLAB 可变参数说明与 Baltam_IR 处理方案

## 目标

这份文档分两部分：

- 先说明 MATLAB 中 `varargin` / `varargout` / `nargin` / `nargout` 的语义边界
- 再给出 `Baltam_IR` 中如何表达和执行这套语义的方案

本文只讨论函数级可变参数，不讨论 classdef、arguments block、method dispatch 等更高层语义。

## MATLAB 语义

### 1. `varargin`

`varargin` 是函数的可变输入参数。

典型写法：

```matlab
function f(a, b, varargin)
```

语义是：

- `a`、`b` 是固定位置参数
- 额外传入的参数会被打包进 `varargin`
- 在函数体内，`varargin` 表现为一个 `1 x N` 的 `cell`

例如：

```matlab
function f(a, b, varargin)
```

调用：

```matlab
f(1, 2, "x", 3)
```

则函数体内可观察到：

- `a = 1`
- `b = 2`
- `varargin = {"x", 3}`

### 2. 少传固定参数时不会在入口直接报错

MATLAB 对固定参数的关键点是：

- 少传固定参数时，函数可以进入
- `nargin` 等于实际传入参数个数
- 只有在函数体真正读取缺失的那个固定参数时，才会报错

例如：

```matlab
function f(a, b, c)
    disp(nargin);
    x = a + b;
end
```

调用：

```matlab
f(1, 2)
```

函数可以进入，且：

- `nargin == 2`
- 如果函数体不读取 `c`，就不会因为缺少 `c` 直接失败

这点和“调用前就按形参数量严格校验”不同。

### 3. 多传固定参数时的行为

如果函数头没有 `varargin`，而调用时传入参数个数超过固定形参数量，则会在进入函数前报错。

如果函数头有 `varargin`，则多出来的参数会被打包进 `varargin`，不会报错。

### 4. `varargout`

`varargout` 是函数的可变输出参数。

典型写法：

```matlab
function [a, b, varargout] = g(x)
```

语义是：

- `a`、`b` 是固定输出
- 调用方额外请求的输出槽由 `varargout` 提供
- 在函数体内，`varargout` 也表现为一个 `1 x N` 的 `cell`

例如：

```matlab
function [a, b, varargout] = g(x)
    a = x;
    b = x + 1;
    if nargout >= 3
        varargout{1} = x + 2;
    end
    if nargout >= 4
        varargout{2} = x + 3;
    end
end
```

调用：

```matlab
[p, q, r, s] = g(10)
```

则：

- `p = 10`
- `q = 11`
- `r = 12`
- `s = 13`

### 5. `nargin` 和 `nargout`

在函数体内：

- `nargin` 表示这次调用实际传入的输入参数个数
- `nargout` 表示这次调用请求的输出参数个数

它们是调用时动态决定的，不是只看函数声明。

例如：

```matlab
function [a, b, varargout] = g(x)
```

如果调用：

```matlab
[p] = g(10)
```

则函数体内：

- `nargin == 1`
- `nargout == 1`

如果调用：

```matlab
[p, q, r] = g(10)
```

则函数体内：

- `nargin == 1`
- `nargout == 3`

### 6. `varargin` / `varargout` 在函数体内的运行时形态

对函数体来说：

- `varargin` 是普通 `cell`
- `varargout` 也是普通 `cell`

因此下面这些都是普通元胞语义：

- `isempty(varargin)`
- `size(varargin)`
- `varargin{1}`
- `varargin{2:3}`
- `varargout{i} = x`

可变参数真正特殊的地方不在函数体内部，而在函数边界：

- 入口如何绑定输入
- 返回时如何展开输出
- `nargin` / `nargout` 如何取值

### 7. 展开语义

MATLAB 经常把 cell 内容展开为多输入或多输出：

```matlab
f(C{:})
[varargout{1:nargout}] = g(varargin{:})
```

这类语义本质上不是“把 cell 当一个参数传递”，而是：

- 先从 cell 中按顺序取出元素
- 再把这些元素作为独立的位置参数或位置输出处理

因此实现时通常需要一个“展开桥接层”。

### 8. 名字和位置

这里需要区分“名字恰好叫某个字符串”和“它在函数签名中承担特殊角色”。

应当按位置解释特殊含义：

- 输入列表最后一个位置如果是 `varargin`，它表示可变输入
- 输出列表最后一个位置如果是 `varargout`，它表示可变输出

类似：

```matlab
function [varargout1, c] = h(varargin)
```

中的 `varargout1` 只是普通名字，不是可变输出槽。

仓库中已有相关例子：

- [test4.m](/home/zj/Desktop/Baltam_IR/test/m/test4/test4.m)
- [test4_2.m](/home/zj/Desktop/Baltam_IR/test/m/test4_2/test4_2.m)

## Baltam_IR 处理方案

## 总体原则

当前更合适的方案是：

- 不为 `varargin` / `varargout` 新增专门的 IR 节点
- 把它们视为“函数签名上的特殊约定”
- 函数体内部仍然把它们当普通变量使用

也就是说，特殊性属于：

- `Function` 签名元信息
- 调用边界
- 返回边界
- 解释器执行帧

而不属于：

- `AssignNode`
- `CallNode`
- `ReturnNode`
- `SSACallNode`
- `SSAReturnNode`

## 为什么不单独增加 variadic IR 节点

原因很直接：

- 函数体里对 `varargin` / `varargout` 的读写，本质上是普通 cell 操作
- 当前 IR 已经能表达普通变量、调用、多返回值和 cell get/set
- 真正特殊的是“调用边界如何把参数打包进来、如何从输出里再展开出去”

因此单独加 `VarArgInNode` / `VarArgOutNode` 这类节点只会让 IR 变复杂，而不会减少解释器中的调用约定逻辑。

## Function 层需要保留什么信息

### 1. 需要保留 variadic 标志

`Function` 中应保留：

- `has_varargin`
- `has_varargout`

这两个标志是 IR 层的函数签名元信息。

它们的作用是：

- 让解释器在没有 AST 的情况下也能知道该函数是否是 variadic
- 让 lowering、SSA 构造、verifier、printer 对同一份签名语义有一致理解

### 2. 不需要额外保存固定参数个数

固定输入/输出个数可以由签名直接推导：

- 固定输入个数 = `inputs().size() - (has_varargin ? 1 : 0)`
- 固定输出个数 = `outputs().size() - (has_varargout ? 1 : 0)`

因此不必再额外存：

- `fixed_input_count`
- `fixed_output_count`

这些信息在 lowering 阶段当然能从 AST 得到，但在 IR 中没有必要冗余保存。

### 3. 不需要为 variadic 本身再加额外的 NamedValue 元信息

`varargin` / `varargout` 的名字是固定约定，不需要再额外挂“variadic 名字对象”这类元信息。

但为了和当前 IR / SSA 入口机制兼容，函数体里仍然需要存在可绑定的值槽：

- `varargin` 作为函数体内可见的输入绑定
- `varargout` 作为函数体内可见的输出绑定

也就是说：

- 不需要额外的“variadic NamedValue 元信息”
- 但函数体里仍然需要能以普通名字读取或写入 `varargin` / `varargout`

## 函数体内的运行时表示

### 1. `varargin`

在函数体内部，`varargin` 应统一表示为：

- `cell_array`

原因：

- MATLAB 语义上就是 `1 x N cell`
- `isempty(varargin)`、`size(varargin)`、`varargin{1}` 都可以直接走普通 cell 语义

### 2. `varargout`

在函数体内部，`varargout` 也应统一表示为：

- `cell_array`

这可以让：

- `varargout{i} = ...`
- `c = varargout{1}`

都继续走普通 cell get/set 语义。

### 3. `var_list` 的定位

`var_list` 更适合作为“展开桥接的临时表现”，而不是函数体内 `varargin` / `varargout` 的正式承载类型。

推荐定位是：

- `C{:}` 展开后的临时结果
- 调用前把“展开结果”摊平成多个位置实参
- 返回时把需要展开的可变输出转成位置返回值序列

因此应区分：

- 函数体内正式变量：`cell_array`
- 调用边界展开桥接：`var_list`

## lowering 方案

lowering 不需要增加新的 IR 节点类别，但需要识别函数签名中的 variadic 约定。

应做的事情：

1. 读取函数头输入/输出列表
2. 判断最后一个输入是否是 `varargin`
3. 判断最后一个输出是否是 `varargout`
4. 把 `has_varargin` / `has_varargout` 写入 `Function`
5. 函数体内对 `varargin` / `varargout` 的读写继续按普通变量处理

也就是说：

- variadic 是 `Function` 的签名信息
- 不是独立的 AST 到 IR 节点翻译问题

## NonSSA / SSA 表示方案

当前方案不需要新增：

- `VarArgInputNode`
- `VarArgOutputNode`
- `NarginNode`
- `NargoutNode`

保留现有表达方式即可：

- `CallNode`
- `ReturnNode`
- `SSACallNode`
- `SSAReturnNode`

其中：

- 调用点请求的输出个数，本来就由 call 结果个数决定
- `nargout` 在 callee 中应读取“本次调用请求的输出数”
- 不需要在 call 节点上额外再加一个 `requested_nargout` 字段

## 解释器执行帧方案

解释器执行帧中应增加两类调用边界信息：

- `actual_nargin`
- `requested_nargout`

这两个值属于“本次函数调用上下文”，不属于 IR 本身。

### 1. `actual_nargin`

取调用方本次真实传入的参数个数。

它决定：

- `nargin` 的结果
- 固定形参里哪些参数缺失
- 是否存在“额外实参要打包进 `varargin`”

### 2. `requested_nargout`

取调用点请求的输出个数。

它决定：

- `nargout` 的结果
- 返回时要收集多少个固定输出
- 是否还要继续从 `varargout` 中展开输出

## 入口参数绑定方案

假设函数签名是：

```matlab
function [o1, o2, varargout] = f(a, b, varargin)
```

则解释器入口绑定建议如下。

### 1. 固定输入部分

前两个固定输入是：

- `a`
- `b`

绑定规则：

- 如果调用方提供了该位置实参，则正常绑定
- 如果没有提供，则该输入位置记为“缺失固定参数”

### 2. 少传固定参数时不直接报错

这点必须显式支持。

错误行为不应是：

- 入口处直接因为 `args.size() != formal_count` 而失败

而应是：

- 允许函数进入
- 把缺失的固定形参标成缺失状态
- 只有真正读取该缺失参数时才报错

因此解释器中的运行时值建议增加一种专门状态，例如：

- `MissingInput`

不要把它和现有 `Undef` 混在一起：

- `Undef` 表示 SSA 语义中的未定义值
- `MissingInput` 表示 MATLAB 调用约定中的“未实传固定形参”

这两者来源不同，报错语义也不同。

### 3. `varargin` 绑定

如果函数带 `varargin`：

- 多出来的实参按顺序打包成 `1 x N cell`
- 绑定到函数体内的 `varargin`

如果没有多余实参：

- `varargin` 绑定为空的 `1 x 0 cell`

如果函数不带 `varargin`：

- 多传实参应在入口直接报错

## 返回值绑定方案

### 1. 固定输出部分

对固定输出：

- 按声明顺序收集

### 2. `varargout`

如果调用方请求的输出个数超过固定输出个数，且函数带 `varargout`：

- 从 `varargout` 这个 `cell_array` 中按顺序继续展开

例如：

```matlab
function [a, varargout] = f(...)
```

如果调用方请求 3 个输出：

- 第 1 个输出来自固定输出 `a`
- 第 2、3 个输出来自 `varargout{1}`、`varargout{2}`

### 3. `varargout` 的初始化

更稳妥的实现是：

- 函数入口就把 `varargout` 初始化为空 `cell_array`

这样函数体可以直接执行：

- `varargout{i} = ...`

而不需要在第一次写入时再做特殊懒初始化。

## `nargin` / `nargout` 的处理方式

当前更适合把它们当作解释器内建的特殊读取语义，而不是普通 builtin。

理由：

- 它们读的是“当前执行帧”的动态信息
- 不应依赖外部 builtin table 或 runtime 工作区查找

建议：

- 在解释器里把 `nargin` / `nargout` 作为特殊名字或特殊 direct-call 处理
- 它们直接返回当前执行帧中的 `actual_nargin` / `requested_nargout`

## 与现有仓库结构的对应关系

### 1. IR

需要改动：

- [src/ir/ir.h](/home/zj/Desktop/Baltam_IR/src/ir/ir.h)
- [src/ir/ir.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir.cpp)

增加 `Function` 的 variadic 标志。

### 2. lowering

需要改动：

- [src/lowering/lowering.cpp](/home/zj/Desktop/Baltam_IR/src/lowering/lowering.cpp)

责任是：

- 从函数签名中识别 `varargin` / `varargout`
- 把 variadic 信息写进 `Function`

### 3. SSA 构造

需要改动：

- [src/optimizer/construct_untyped_ssa.cpp](/home/zj/Desktop/Baltam_IR/src/optimizer/construct_untyped_ssa.cpp)

责任是：

- 把 `Function` 的 variadic 信息从 non-SSA 复制到 untyped SSA

### 4. 解释器

需要改动：

- [src/interpreter/interpreter.cpp](/home/zj/Desktop/Baltam_IR/src/interpreter/interpreter.cpp)
- [src/interpreter/interpreter.h](/home/zj/Desktop/Baltam_IR/src/interpreter/interpreter.h)

责任是：

- 记录 `actual_nargin`
- 记录 `requested_nargout`
- 支持缺失固定参数
- 支持 `varargin` 打包
- 支持 `varargout` 展开
- 支持 `nargin` / `nargout` 读取

## 推荐实施顺序

### Phase 1

补 `Function` 的 variadic 签名标志，并让 lowering / SSA builder 传递这份信息。

### Phase 2

修改解释器入口绑定逻辑：

- 去掉“实参数量必须等于形参数量”的假设
- 支持缺失固定参数
- 支持 `varargin` 打包

### Phase 3

修改解释器返回逻辑：

- 支持按请求输出数展开 `varargout`

### Phase 4

补 `nargin` / `nargout` 的执行帧读取逻辑，并补对应回归测试。

## 当前结论

当前更合适的方案是：

- 在 IR 的 `Function` 上显式保留“是否存在 `varargin/varargout`”这类签名信息
- 不为 variadic 新增专门 IR 节点
- 函数体内部统一把 `varargin/varargout` 当普通 `cell_array`
- 把 `var_list` 限定为展开桥接层的临时表现
- 在解释器调用边界实现真正的可变参数语义

这样既能保住当前 IR 结构的简洁性，也能把 MATLAB 可变参数的特殊性限制在真正需要特殊处理的边界位置。
