# MATLAB 多输出与 comma-separated list 赋值语义

本文记录 MATLAB 多返回值调用、bracket 多输出赋值，以及 cell comma-separated
list 在输出位置的运行期语义。结论基于本机 MATLAB CLI 探针验证。

## 1. 基本多输出调用

MATLAB 调用点请求多少个输出，由调用上下文决定：

```matlab
y = f(x);        % 请求 1 个输出
[a, b] = f(x);   % 请求 2 个输出
f(x);            % 请求 0 个输出
```

函数内部的 `nargout` 不是编译期常量，而是运行期从当前调用帧读取的请求输出个数。

占位符 `~` 仍然占用输出位：

```matlab
[~, b] = f(x);   % 请求 2 个输出，第 1 个输出被丢弃
```

## 2. Bracket 多输出赋值

`[lhs1, lhs2, ...] = rhs` 是 comma-separated list assignment。右侧如果是函数调用，
函数调用请求的输出个数等于左侧输出列表展开后的槽位数量。

```matlab
C = cell(1, 4);
[a, C{2:end}, b] = fun();
```

这里 `C{2:end}` 展开为 `C{2}, C{3}, C{4}`，因此 `fun` 请求 5 个输出：

```text
a      <- output 1
C{2}   <- output 2
C{3}   <- output 3
C{4}   <- output 4
b      <- output 5
```

`C{2:end}` 可以出现在输出列表任意位置：

```matlab
[C{2:end}, a] = fun();
[a, C{2:end}, b] = fun();
[a, C{2:end}] = fun();
```

它不是“尾部 varargout receiver”，而是输出列表中的普通动态输出节点。

## 3. `C{:}` 与 `C{2:end}` 需要已有变量

在 bracket 多输出赋值中，`C{:}` 需要根据已有 `C` 展开输出槽位。因此 `C` 不存在时，
MATLAB 在右侧函数和参数求值之前报错：

```matlab
clear C
[a, C{:}] = fun(g());
```

实测结果中 `g()` 和 `fun()` 都不会执行，错误为：

```text
MATLAB:index:assignment_to_uninitialized_using_colon
Comma-separated list assignment to a nonexistent variable is not supported when any index is a colon (:).
```

`C{2:end}` 也需要已有 `C`，因为 `end` 依赖当前数组尺寸。`C` 不存在时，实测错误为：

```text
MATLAB:lang:EndUsedOutsideIndexingExpression
The end operator must be used within an array index expression.
```

如果 `C` 已存在但不是 cell，`C{:}` 和 `C{2:4}` 都会在右侧求值前报错：

```text
MATLAB:cellAssToNonCell
Unable to perform assignment because brace indexing is not supported for variables of this type.
```

## 4. `C{2:4}` 和 `C{idx}` 可以创建 cell

显式索引列表不需要读取已有 `C` 的尺寸，因此可以在 bracket 多输出赋值中创建或扩展
cell 变量：

```matlab
clear C
[a, C{2:4}] = fun();
```

实测 `fun` 请求 4 个输出，结果类似：

```matlab
a = 1
C = {[], 2, 3, 4}
```

变量索引也可以：

```matlab
clear C
idx = [2 4];
[a, C{idx}] = fun();
```

实测 `fun` 请求 3 个输出，结果类似：

```matlab
a = 1
C = {[], 2, [], 3}
```

因此需要区分：

```text
C{:}        需要已有 C
C{2:end}    需要已有 C，因为 end 依赖 C
C{2:4}      C 可以不存在
C{idx}      idx 可求值即可，C 可以不存在
```

如果 `C` 存在但 `2:end` 为空，例如：

```matlab
C = cell(1, 1);
[a, C{2:end}] = fun();
```

则 `C{2:end}` 展开为 0 个输出槽位，`fun` 只请求 1 个输出。

## 5. Bracket 赋值和普通赋值不同

下面两种形式不是同一种语义：

```matlab
[C{:}] = fun();
C{:} = fun();
```

第一种是 bracket comma-separated list assignment。`C{:}` 参与计算右侧函数的输出个数，
`C` 不存在时右侧不执行。

第二种是普通 indexed assignment。右侧处于普通表达式上下文，因此函数只请求 1 个输出：

```matlab
clear C
C{:} = fun();
```

实测：

```text
fun called, nargout=1
C = {[1]}
```

普通赋值中：

```matlab
clear C
x = [10 20 30 40];
C{:} = x(2:3);
```

会创建 `C = {[20 30]}`，不会把 `20` 和 `30` 拆成两个 cell。

如果 `C` 已经有多个元素：

```matlab
C = cell(1, 2);
C{:} = fun();
```

仍然先以 1 个输出调用 `fun`，然后赋值阶段报错：

```text
MATLAB:index:expected_one_output_for_assignment
Assigning to 2 elements using a simple assignment statement is not supported.
```

因此 IR lowering 必须按上下文区分同样的 `C{:}`：

```text
[C{:}] = f()    -> call output node / comma-separated output receiver
C{:} = f()      -> indexed assignment target，RHS 是 1-output expression
```

## 6. 输出计数和写回不是同一个动作

对 bracket 多输出赋值，MATLAB 会在右侧参数求值前先根据输出列表计算本次函数调用的
requested output count。

实测：

```matlab
clear C
[a, C{:}] = fun(g());
```

`C` 不存在时，`g()` 不执行。

如果 `C` 先存在：

```matlab
C = cell(1, 2);
[a, C{:}] = fun(g());
```

而 `g()` 在 caller workspace 中把 `C` 改成 `cell(1, 5)`，则 `fun` 内看到的
`nargout` 仍然是 3，也就是调用前按旧 `C` 算出的输出个数。但返回后的赋值阶段会受
当前 workspace 中 `C` 的状态影响，可能因为输出不足而报错。

这说明：

```text
调用前：输出节点用于计算 requested nargout，并执行必要的前置检查
调用后：输出节点再执行赋值写回
```

不能简单地把 `C{:}` 在调用前冻结成一组永久 receiver 后直接复用。

## 7. IR 建模建议

`CallInst` 不应只用 `std::vector<ValueId> results` 表达输出。对 MATLAB 来说，call 的输出
既可能是普通 SSA value，也可能是一个需要运行期展开和赋值的输出节点。

推荐把 call 输出建模为 output specification：

```cpp
struct CallOutput {
    enum class Kind {
        Value,      // 普通 SSA value
        Discard,    // 对应 ~，仍计入 nargout
        Node        // LHS 输出节点，例如 C{:}, C{2:end}, C{idx}, A(i)
    };

    ValueId value;
    OutputNodeId node;
};
```

`CallInst` 保存：

```cpp
std::vector<CallOutput> outputs;
```

执行 `CallInst` 时：

```text
1. 遍历 outputs，调用每个 output node 的 count_for_call(ctx)
2. 得到 requested_nargout，并执行 RHS 前必须发生的错误检查
3. 求值 callee 和输入参数
4. 调用 callee，并把 requested_nargout 放入 callee frame
5. callee 内部的 nargout() 从当前 frame 读取 requested_nargout
6. callee 返回后，再遍历 outputs 执行 assign_from(ctx, returns, index)
```

这样不需要把 `nargout` 单独建成 IR value，也不需要独立的 `prepare_outputs` 指令；
但 `CallInst.outputs` 必须保留足够的输出节点信息，以便运行期在正确时序下计算输出个数和执行写回。

静态场景仍可特化：

```matlab
[a, b] = f(x)
[~, b] = f(x)
```

可以继续打印成固定多结果 call；但语义上 `~` 需要保留输出位次，动态输出节点需要参与
运行期 output count。
