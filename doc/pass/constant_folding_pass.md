# Constant Folding Pass

源码位置：`src/pass/constant_folding_pass.cpp`

Pass 名称：`constant-folding`

作用域：`IRPassScope::CodeUnit`

当前状态：已实现第一版

## 优化目标

该 pass 对 IR 中可静态求值的一元运算符、二元运算符，以及白名单内的 direct builtin call
做常量折叠。第一版只支持标量 runtime object，并通过已有 builtin 计算结果，不在优化器里
重新实现 MATLAB 数值语义。

示例：

```text
[%0, double] = const 1
[%1, double] = const 2
[%2, double] = add %0, %1
store %slot0, %2
```

可改写为：

```text
[%0, double] = const 1
[%1, double] = const 2
[%2, double] = const 3 ; folded
store %slot0, %2
```

`%2` 的 `ValueId` 保持不变，原 `BinaryInst` 原地替换为 `ConstInst`，因此不需要全局
改写后续 use。

## 常量表示

`ConstInst` 仍然是唯一的常量指令。为了承载 builtin 返回的运行期对象，`Constant` variant
新增一种 payload：

```cpp
struct RuntimeObjectConstant {
    const_ba_obj_ptr value;
    bool folded = true;
};
```

设计含义：

- `RuntimeObjectConstant` 是 IR literal，不是普通运行期临时变量。
- builtin 的返回值直接作为常量载体进入 IR，不再反解为一套独立的 C++ 标量 constant。
- 第一版只允许 `RuntimeObjectConstant` 保存支持范围内的 scalar `ba_obj`。
- executor 读取该常量时必须避免修改 IR 持有的对象；保守实现可以每次返回 clone。

已有 `LogicalConstant`、`Int64Constant`、`UInt64Constant`、`Float64Constant` 等常量可以继续
保留。第一版常量折叠的产物统一生成 `RuntimeObjectConstant`。

## 支持范围

第一版折叠一元运算符、二元运算符，以及白名单内的 direct builtin call，且输入和输出都必须是标量。

支持的 runtime 类型：

- `ba_int8_mat`
- `ba_int16_mat`
- `ba_int32_mat`
- `ba_int64_mat`
- `ba_uint8_mat`
- `ba_uint16_mat`
- `ba_uint32_mat`
- `ba_uint64_mat`
- `ba_single_mat`
- `ba_double_mat`
- `ba_bool_mat`

不处理：

- 矩阵常量
- complex
- char / string
- cell / struct
- 一般函数调用。当前只把 `sin` 纳入 direct builtin call 白名单。
- `eval`、`feval`、函数句柄等动态场景

## builtin 计算

pass 不直接计算 `1 + 2`。它只负责把 IR operand 转换为 `ba_obj`，然后调用 builtin。

核心 helper：

```cpp
bool is_foldable_inst(const CodeUnit& unit, const Instruction& inst);

bool is_supported_scalar_runtime_object(const ba_obj& obj);

std::optional<const_ba_obj_ptr> ba_obj_from_constant(const Constant& constant);

const char* builtin_name_for_unary(UnaryOp op);
const char* builtin_name_for_binary(BinaryOp op);
```

折叠流程：

1. 扫描 `UnaryInst` / `BinaryInst` / 白名单内的 direct `CallInst`。
2. 调用 `is_foldable_inst` 判断该指令当前是否允许折叠。
3. 检查所有 operand 都由 `ConstInst` 定义。
4. 将 operand `Constant` 转为 `ba_obj`。
5. 检查输入 `ba_obj` 是支持的 scalar 类型。
6. 根据 operator 查到 builtin 名。
7. 通过 `lookup_builtin_function` 查找 builtin。
8. 调用 builtin。
9. 检查返回值存在、唯一、是支持的 scalar 类型。
10. 用返回的 runtime object 构造 `RuntimeObjectConstant`。
11. 原地替换当前 operator 指令为 `ConstInst`，保留原 result `ValueId`。

所有 MATLAB 数值提升、整数溢出、single/double 保留、logical 结果等语义都由 builtin 决定。

## 运算符映射

一元运算符：

```text
Uplus      -> uplus
Uminus     -> uminus
LogicalNot -> not
Transpose  -> transpose
Ctranspose -> ctranspose
```

二元运算符：

```text
Add      -> plus
Sub      -> minus
Mul      -> mtimes
Rdiv     -> mrdivide
Ldiv     -> mldivide
Pow      -> mpower
ElemMul  -> times
ElemRdiv -> rdivide
ElemLdiv -> ldivide
ElemPow  -> power
And      -> and
Or       -> or
Lt       -> lt
Le       -> le
Gt       -> gt
Ge       -> ge
Eq       -> eq
Ne       -> ne
```

## call 折叠白名单

当前 direct call 只支持：

```text
sin
```

`sin` 的折叠条件是：

```text
callee 是 direct @sin
所有实参都由 ConstInst 定义
返回值数量是 1
输入和输出都是支持的 scalar runtime object
```

后续扩展更多函数时，应先确认函数是纯函数，且不会依赖 workspace、随机状态、I/O、时间、path
或其他运行期环境。

## 可折叠判断

当前阶段采用一个简化假设：

```text
只要一元 / 二元运算符的所有 operand 都是常量，就可以尝试折叠。
白名单 direct call 只要所有实参都是常量，就可以尝试折叠。
```

因此第一版的 `is_foldable_inst` 可以只检查：

```text
inst 是 UnaryInst / BinaryInst，且所有 operand 都由 ConstInst 定义
或者 inst 是白名单内的 direct CallInst，且所有 argument 都由 ConstInst 定义
```

`dispatch_type` 暂时不作为折叠准入条件。即使普通源码运算符当前仍是 `Dynamic`，只要操作数都是
常量，也会通过 builtin 名称映射和 `lookup_builtin_function` 尝试折叠。

为避免后续修改 pass 主体，分派和名字解析相关规则必须集中在 `is_foldable_inst` 这一层。
未来如果需要收紧 MATLAB 语义边界，例如排除变量、local function、nested function、import、
private function 等竞争者，只修改该判断函数或其依赖的策略对象；常量提取、builtin 调用、
`RuntimeObjectConstant` 生成和指令替换流程保持不变。

## 打印格式

IR printer 保留现有 `const` 格式，不新增 `const.runtime_object` 语法。折叠后的
`RuntimeObjectConstant` 只在尾部加标记：

```text
[%2, double] = const 3 ; folded
[%3, int8] = const 3 ; folded
[%4, logical] = const true ; folded
[%5, single] = const 1.5 ; folded
```

打印规则：

- 主格式仍为 `[%value, type] = const <value>`。
- `<value>` 来自 runtime object 的一行字符串表示。
- 如果可用，优先使用 `baltam::internal::obj2str_one_line(*obj)`。
- 不建议用 `as_double()` 作为正式打印路径，因为它会丢失 `single`、整数宽度、`uint64` 大值和
  `logical` 等类型信息。
- `type` 仍由 `ValueTable` / `TypeFact` 打印；实现已从 `ba_obj::type()` 设置 result value
  的类型信息。
- `TypeSet` 已补齐第一版需要的 `int8/int16/int32/int64/uint8/uint16/uint32/uint64/single/double/logical`
  标量类型，避免 IR 文本中的类型误导后续优化和调试。

## 错误处理

下面情况全部跳过，不作为 pass fatal error：

- operand 不是常量
- operand 无法转换为支持的 scalar `ba_obj`
- operator 没有 builtin 映射
- `lookup_builtin_function` 失败
- builtin 调用抛异常
- builtin 没有产生唯一返回值
- 返回值不是支持的 scalar 类型

跳过折叠后保留原指令，由运行期继续按原语义执行。

## Pass 顺序

默认 cleanup pipeline 中在 load forwarding 之后运行，当前运行两轮：

```text
load-forwarding
constant-folding
dead-branch-elimination
cfg-simplification
load-forwarding
constant-folding
constant-deduplication
dead-code-elimination
```

原因：

- `load-forwarding` 可以先把 `store; load` 形状改成直接引用常量。
- 第一轮 `constant-folding` 获得更多常量 operand，并暴露常量分支。
- `dead-branch-elimination` 和 `cfg-simplification` 消费常量条件带来的 CFG 简化机会。
- 后置 `load-forwarding` 清理 CFG 合并后暴露出的同 block `store; load`。
- 后置 `constant-folding` 继续折叠 load forwarding 暴露出的常量运算，例如 `mul`。
- `constant-deduplication` 再合并折叠出来的重复常量，并处理 loop constant hoist。
- `dead-code-elimination` 删除常量折叠、死分支和 CFG 简化后遗留的 unused const。

后续可以把这组 pass 做成固定点循环。

## 测试计划

第一版测试覆盖：

- 一元：`+x`、`-x`、`~x` / `not(x)`。
- 二元算术：`+`、`-`、`*`、`/`、`\`、`^`、`.*`、`./`、`.\`、`.^`。
- direct call：`sin(x)`，其中 `x` 是常量。
- 比较：`<`、`<=`、`>`、`>=`、`==`、`~=`，返回 logical。
- 逻辑：`and` / `or`。
- 11 种支持 scalar 类型的输入和返回类型。
- `RuntimeObjectConstant` 的 IR 打印格式。
- unsupported 常量保持不变。
- builtin lookup 失败或调用异常时 pass 不报错。
- 当前阶段 `Dynamic` operator 只要 operand 都是常量也会尝试折叠。

测试应在 pass 后运行 `IRVerifier`，并用当前 IR executor 验证折叠前后结果一致。
