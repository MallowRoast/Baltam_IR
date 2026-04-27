# ValueId 类型事实与函数分派设计

## 目标

本文记录一种面向“函数分派收敛”的最小类型设计。目标不是把当前主 `IR` 直接改造成强类型 `IR`，而是为 `ValueId` 增加一层可选的类型事实（type fact），用于：

- 缩小运行时调用分派的候选集
- 为 builtin fast path、local method 选择和调用缓存提供输入
- 为后续 `typed SSA` 提供前置类型信息来源

这里的前提分层与 [ir_draft.md](./ir_draft.md) 和 [execution_strategy.md](./execution_strategy.md) 保持一致：

- 主 `IR` 仍然是语义层、non-SSA 的高层表示
- `bytecode` 和解释器仍然是稳定语义基线
- `typed SSA` 仍然只在热点稳定 region 上按需构造

## 1. 当前问题

当前 `IR` 中已经有：

- `ValueId`
- `SlotId`
- `ApplyInst`
- `CallInst`

但还没有一套正式的数据结构来表达：

- 某个 `ValueId` 当前已知是什么类型
- 这些类型信息有多可靠
- 哪些调用点已经结合参数类型缩小了候选目标

对于 Matlab 这类语言，调用分派通常同时受两类信息影响：

1. 名字解析结果是否稳定
2. 实参类型是否足以排除大部分候选目标

因此，如果只知道名字，不知道参数类型，很多 builtin 或方法分派仍然会落到慢路径；反过来，如果只知道参数类型，不知道名字是否被 shadow，也同样不能直接静态收敛调用。

## 2. 核心判断

### 2.1 不把主 IR 直接改成“强类型 IR”

当前阶段更合适的做法不是要求每个 `ValueId` 都携带一个永远精确的静态类型，而是：

- 让 `ValueId` 关联一份“类型事实”
- 允许这份事实是保守的、不完整的、由 profile 或 guard 支持的
- 让调用优化和后续 `typed SSA` 消费这些事实

原因主要有三点：

1. 当前主 `IR` 的职责是表达 Matlab 动态语义，而不是承载主要优化框架。
2. 很多值在高层 `IR` 上只能得到上界类型，而不是精确类型。
3. 分派优化真正需要的是“足够缩小候选集的事实”，而不是全局常驻的强类型主干表示。

### 2.2 类型事实服务于分派，但不替代名字解析

这套设计必须坚持一个边界：

- `ValueId -> TypeFact` 负责回答“这个值像什么”
- `CallSite -> ResolutionFact` 负责回答“这个调用点当前可能在调谁”

两者是正交信息。

例如脚本中的 `sin(a)`：

- 即使 `a` 已知是 `float64`
- 也不能只凭参数类型断定 `sin` 一定是 builtin

因为它仍可能受 workspace 遮蔽、local 函数、`private`、路径变化等因素影响。

## 3. 第一版范围

第一版只解决“分派导向”的最小问题，不追求一次覆盖完整 Matlab 类型系统。

当前建议范围：

- 先只对 `FunctionUnit` 做正式类型数据流
- `ScriptUnit` 中的 `LoadWorkspaceInst` 结果默认保守为 `Any`
- 先覆盖常量、slot 数据流、基础一元/二元算子、少量 builtin 摘要
- 先不处理 `global`、`persistent`、closure 捕获、`eval` 引入的新名字

如果后续脚本名字稳定区间分析已经把某些名字收敛成 slot，再把脚本里的对应区段逐步纳入同一套分析。

## 4. 类型事实模型

### 4.1 `DispatchTag`

第一版不需要一上来就构造很复杂的类型格，先保留一组足够服务调用分派的粗粒度标签即可：

```cpp
enum DispatchTag : std::uint16_t {
    Any,

    Logical,
    Int64,
    UInt64,
    Float64,
    Complex128,

    CharArray,
    StringScalar,
    CellArray,
    StructArray,

    FunctionHandle,
    WorkspaceHandle,
    UserObject,
};
```

这些标签的目标不是完整复刻运行时对象模型，而是：

- 快速区分数值 builtin 路径
- 区分对象方法分派与普通 builtin 分派
- 区分函数句柄调用与名字调用

### 4.2 `TypeCertainty`

仅有“类型标签”还不够，还需要知道这个结论有多可靠：

```cpp
enum TypeCertainty : std::uint8_t {
    Unknown,
    UpperBound,
    Exact,
    GuardedExact,
};
```

建议语义如下：

- `Unknown`
  目前没有形成可用判断
- `UpperBound`
  只知道一个保守上界，例如“某值一定是数值类中的某一类”
- `Exact`
  静态上已经能确定精确类型
- `GuardedExact`
  只有在某组 runtime guard 成立时才可视为精确类型

### 4.3 `TypeFact`

第一版更合适的结构大致如下：

```cpp
struct TypeFact {
    DispatchTag tag = Any;
    TypeCertainty certainty = Unknown;

    InternedString class_name;

    bool known_scalar = false;
    bool is_scalar = false;

    bool known_complex = false;
    bool is_complex = false;

    bool has_shape = false;
    std::vector<std::int64_t> dims;
};
```

字段含义：

- `tag`
  供调用分派快速使用的主分类
- `certainty`
  说明该事实是精确结论还是仅为保守上界
- `class_name`
  主要给 `UserObject` 或特殊 handle 类值使用
- `known_scalar/is_scalar`
  区分标量 fast path 与一般数组路径
- `known_complex/is_complex`
  区分实数与复数路径
- `has_shape/dims`
  为后续更细的 builtin 摘要或 `typed SSA` 预留

## 5. IR 中的承载位置

### 5.1 类型事实挂在 `ValueId` 上，而不是直接挂在 `Slot` 上

更合适的原则是：

- `Slot` 表示存储位置
- `ValueId` 表示某条定义出来的数据流值

同一个 `Slot` 在不同程序点可以存放不同类型的值，因此 `Slot` 本身不应被视为“当前值类型”的唯一载体。真正适合挂类型事实的是 `ValueId`。

### 5.2 建议增加 `ValueInfo / ValueTable`

当前 `CodeUnit` 已经拥有 `SlotTable`。对称地，可以再增加一张 `ValueTable`：

```cpp
struct ValueInfo {
    ValueId value_id = InvalidValueId;
    TypeFact type_fact;
    Instruction* def = nullptr;
};

struct ValueTable {
    std::vector<ValueInfo> values;

    ValueInfo* find(ValueId value_id) noexcept;
    const ValueInfo* find(ValueId value_id) const noexcept;
};
```

然后在 `CodeUnit` 上增加：

```cpp
ValueTable value_table;
```

这样做的好处是：

- 不破坏当前 `Instruction` 继承层次
- 多结果调用仍可按 `results : ValueId[]` 逐项记录类型事实
- printer、verifier、类型分析和后续 `typed SSA lowering` 都能共享这张表

### 5.3 `Slot` 只保留可选的类型约束

`Slot` 仍然可以携带一份轻量“声明约束”或“运行时固定角色类型”，但它不应直接等同于当前值类型。

例如：

- `WorkspaceHandle` hidden slot 的运行时对象类型是固定的
- 以后若支持 builtin ABI 或参数注解，也可以把约束挂在 `Arg/Ret slot` 上

但 `load_slot` 产生的 `ValueId` 类型，仍应由 reaching defs 或数据流分析得出。

## 6. 与调用分派相关的解析事实

为了避免误解，建议把“类型事实”和“调用解析事实”同时建模：

```cpp
struct CalleeCandidate {
    enum Kind : std::uint8_t {
        LocalFunction,
        Builtin,
        MFunction,
        ClassMethod,
        FunctionHandleTarget,
        Unknown,
    };

    Kind kind = Unknown;
    InternedString name;
    FunctionUnit* local_target = nullptr;
};

struct ResolutionFact {
    bool name_stable = false;
    std::vector<CalleeCandidate> candidates;

    std::uint32_t resolver_epoch = 0;
    std::uint32_t path_epoch = 0;
    std::uint32_t workspace_epoch = 0;
};
```

这里的设计目的不是立刻把所有调用点都静态收敛，而是把两个问题分开：

- 名字有没有稳定到只剩一小批候选目标
- 参数类型有没有进一步把候选批次缩到单一目标

## 7. 第一版类型事实如何产生

### 7.1 常量

- `ConstInst`
  直接给出 `Exact` 类型事实

例如：

- `Int64Constant -> Int64`
- `Float64Constant -> Float64`
- `StringLiteralConstant -> StringScalar`
- `EmptyDoubleMatrixConstant`
  当前可先记为 `Float64 + 非标量`，或者保守记为 `Any`

### 7.2 复制与简单表达式

- `CopyInst`
  复制输入值的 `TypeFact`
- `UnaryInst / BinaryInst`
  通过 transfer function 推导结果类型

第一版完全可以只支持最小数值规则，例如：

- `float64 + float64 -> float64`
- `int64 + int64 -> int64` 或保守提升到更宽上界
- 比较运算结果 -> `Logical`

### 7.3 slot 数据流

在 `FunctionUnit` 中：

- `StoreSlotInst(slot, value)` 更新该 slot 的当前抽象状态
- `LoadSlotInst(slot)` 读取该 slot 当前抽象状态并赋给结果 `ValueId`

本质上这是一套 non-SSA 前向数据流分析。

### 7.4 workspace 读取

- `LoadWorkspaceInst`
  第一版默认给 `Any`

除非后续单独做了脚本名字稳定区间分析，否则不应在这一层擅自给出更强类型结论。

### 7.5 调用结果

- `ApplyInst`
  在完成名字消歧前，结果默认保守为 `Any`
- `CallInst`
  若命中已知 builtin 摘要，则可根据实参类型生成更强结果
  否则默认给 `Any`

## 8. 类型事实如何帮助分派

### 8.1 builtin 路径收敛

若名字解析已经稳定到 builtin 候选，参数类型事实可以继续决定：

- 是否可直接进入数值 fast path
- 是否必须走通用 helper
- 是否需要先检查复数、标量或 shape 条件

例如：

- `sin(a)` 且 `a : Float64 Exact`
  可直接命中数值 builtin 路径
- `sin(a)` 且 `a : UserObject`
  应转去对象方法或重载分派路径

### 8.2 local / 用户函数 / 方法之间的筛选

若某个调用点的名字已经稳定，但候选目标仍不唯一，那么参数类型可以帮助过滤：

- 数值参数优先走 builtin
- 对象参数优先检查类方法
- `FunctionHandle` 实参可引导间接调用收敛到已知目标

### 8.3 调用缓存 key

类型事实还可以直接参与 inline cache 或调用缓存的 key：

```text
(callee identity or symbol, arg dispatch tags, relevant epochs)
```

这样即使无法完全静态收敛，也能显著减少重复的运行时查找。

## 9. 数据流与合流规则

### 9.1 分析域

第一版最实用的做法是按 slot 维护抽象状态，再把读取结果投射到 `ValueId`：

```text
IN[block]  : SlotId -> TypeFact
OUT[block] : SlotId -> TypeFact
```

### 9.2 join

控制流合流时，对同一 `SlotId` 的多个来源做 join：

- 相同精确类型可保持不变
- 不同但兼容的类型可退化为共同上界
- 差异过大时直接退回 `Any`

第一版不一定要显式构造复杂 `Union`，完全可以先采用“必要时迅速保守化”的策略。

### 9.3 循环

循环场景通过常规 worklist 迭代到不动点即可。

由于当前目标首先是“辅助分派”，而不是“做最强类型推导”，所以：

- 允许分析较快收敛到保守上界
- 不要求一开始就引入复杂 widening / narrowing 机制

## 10. 动态环境边界

以下点应被视为类型与分派分析中的 barrier 或强保守点：

- `eval`
- `assignin`
- `clear`
- `addpath`
- `cd`
- `rehash`
- `mex` 装载或其他会改变分派环境的操作

在这些点附近：

- 名字解析环境可能失效
- 一部分 slot / workspace 相关假设可能需要回退
- 已缓存的分派结论可能只能在 guard 成立时复用

因此这里的类型事实设计应天然允许：

- 回退到 `Any`
- 降级成 `GuardedExact`
- 交给 bytecode / 解释器继续执行

## 11. 与 typed SSA 的关系

这套设计不是 `typed SSA` 的替代品，而是它的前置信息来源之一。

更自然的关系是：

1. 主 `IR` 上先形成 `ValueId -> TypeFact`
2. 热点 region 提取时，筛出足够稳定的值和调用点
3. `typed SSA` 只消费其中可证明或可 guard 的部分
4. guard 失败时回退到 bytecode

也就是说：

- 主 `IR` 上允许存在大量 `Any`
- `typed SSA` 只吃最有价值、最稳定的那一部分类型事实

## 12. 第一阶段非目标

当前不建议一开始就做以下事情：

- 不把主 `IR` 改造成全局强类型表示
- 不要求每个 `ValueId` 都有精确类型
- 不要求完整 Matlab 类型格
- 不要求一次覆盖脚本 workspace、`global`、`persistent`、closure
- 不要求立刻支持所有 builtin 的精细摘要

先把“函数内纯局部 slot + 少量 builtin + 基础数据流”打通，收益和复杂度比最高。

## 13. 建议落地顺序

建议按以下顺序实现：

1. 在 `CodeUnit` 上增加 `ValueTable`
2. 让 `create_value()` 同步创建 `ValueInfo`
3. 先为 `ConstInst`、`CopyInst` 填入基础 `TypeFact`
4. 增加 `FunctionUnit` 内基于 slot 的前向类型数据流
5. 为少量数值 builtin 建立摘要表
6. 在调用点引入 `ResolutionFact + TypeFact` 的联合收敛逻辑
7. 再把这些事实接给 inline cache、baseline JIT 和后续 `typed SSA`

## 14. 当前阶段的一句结论

如果把本文压缩成一句设计判断，可以写成：

> 当前主 `IR` 不需要直接变成强类型 `IR`；更合适的做法是在 `ValueId` 上维护面向调用分派的类型事实，并将其与名字解析稳定性、epoch guard 和后续 `typed SSA` 明确分层。
