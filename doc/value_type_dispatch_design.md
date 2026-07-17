# ValueId 类型事实与函数分派设计

## 目标

本文记录一种面向“函数分派收敛”的最小类型设计。目标不是把当前主 `IR` 直接改造成强类型 `IR`，而是为 `ValueId` 增加一层可选的类型事实（type fact），用于：

- 缩小运行时调用分派的候选集
- 为 builtin fast path、local method 选择和调用缓存提供输入
- 为后续 `typed SSA` 提供前置类型信息来源

这里的前提分层与 [ir_draft.md](./ir_draft.md) 和 [execution_strategy.md](./execution_strategy.md) 保持一致：

- 主 `IR` 仍然是语义层、non-SSA 的高层表示
- high-level IR 解释器仍然是稳定语义基线
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

- 即使 `a` 已知是 `double`
- 也不能只凭参数类型断定 `sin` 一定是 builtin

因为它仍可能受脚本变量或动态 env 遮蔽、local 函数、`private`、路径变化等因素影响。

## 3. 第一版范围

第一版只解决“分派导向”的最小问题，不追求一次覆盖完整 Matlab 类型系统。

当前实现边界：

- IR builder 只写入“构建时能明确知道”的类型事实
- `ConstInst` 写入精确常量类型事实
- `LoadSlotInst` 只有在读取带 `SlotInfo::value_type` 约束的 slot 时写入类型事实，否则保持
  `unknown`
- 少数 `dispatch_type = Internal` 的 call / binary primitive 拥有构建期摘要；动态分派
  或未知 internal 目标仍保持 `unknown`
- `ScriptVar` slot 读取、`UnaryInst`、`ApplyInst` 以及动态 `BinaryInst / CallInst` 在构建阶段
  默认保持 `unknown`
- `Any` 不作为构建阶段的默认值；它表示后续类型分析已经运行，但只能给出全集上界
- 先在 `FunctionUnit` 中实现正式类型数据流，再逐步扩大到脚本稳定区间
- 先不处理 `global`、`persistent`、closure 捕获、`eval` 引入的新名字

如果后续脚本名字稳定区间分析已经把某些名字收敛成 slot，再把脚本里的对应区段逐步纳入同一套分析。

## 4. 类型事实模型

### 4.1 类型集合表示

第一版不再单独维护 `DispatchTag`。类型事实直接复用现有 `TypeSet`：

- `TypeSet` 底层是 `std::bitset`
- 单 bit 表示单一叶子类型
- 多 bit 表示 union type
- `TypeSet::any()` 在 `is_unknown == false` 时表示已分析但只能给出全集上界
- `TypeSet::bottom()` 表示空集合

### 4.2 `TypeFact`

第一版更合适的结构大致如下：

```cpp
struct TypeFact {
    bool is_unknown = true;
    bool is_scalar = false;
    TypeSet types = TypeSet::any();
};
```

字段含义：

- `is_unknown`
  当前还没有形成可用类型结论。此时 `types` 和 `is_scalar` 都不能作为优化依据
- `is_scalar`
  当前是否可视为标量。只有在 `is_unknown == false` 时才有意义
- `types`
  当前可能类型集合。只有在 `is_unknown == false` 时才有意义；实现上直接复用现有
  `TypeSet`，它本身就是 `std::bitset`-backed 表示

需要特别区分：

- `unknown` 表示还没有可用事实，通常是 IR 构建后的默认状态
- `Any` 表示已经完成某一轮分析，但分析只能给出“可能是任意类型”的保守上界

因此，动态调用结果在构建阶段不应直接标成 `Any`。只有类型推导或调用解析 pass 明确运行后，
才应把无法收窄的结果设为 `Any`。

## 5. IR 中的承载位置

### 5.1 类型事实挂在 `ValueId` 上，而不是直接挂在 `Slot` 上

更合适的原则是：

- `Slot` 表示存储位置
- `ValueId` 表示某条定义出来的数据流值

同一个 `Slot` 在不同程序点可以存放不同类型的值，因此 `Slot` 本身不应被视为“当前值类型”的唯一载体。真正适合挂类型事实的是 `ValueId`。

### 5.2 `ValueInfo / ValueTable`

当前 `CodeUnit` 已经拥有 `SlotTable`。对称地，`CodeUnit` 也持有一张 `ValueTable`：

```cpp
struct ValueInfo {
    ValueId value_id = InvalidValueId;
    std::size_t result_index = 0;
    TypeFact type_fact;
    Instruction* def = nullptr;
};

struct ValueTable {
    std::vector<ValueInfo> values;

    bool empty() const noexcept;
    ValueInfo* find(ValueId value_id) noexcept;
    const ValueInfo* find(ValueId value_id) const noexcept;
};
```

`CodeUnit` 上对应字段为：

```cpp
ValueTable value_table;
```

这样做的好处是：

- 不破坏当前 `Instruction` 继承层次
- 多结果调用仍可按 `results : ValueId[]` 逐项记录类型事实
- printer、verifier、类型分析和后续 `typed SSA lowering` 都能共享这张表

更具体的约束建议如下：

- `ValueTable` 的作用域是单个 `CodeUnit`
- `values[index]` 对应 `ValueId(index)`，也就是按当前 builder 的 unit-local 稠密 `ValueId`
  直接索引
- `ValueInfo::def` 是非拥有指针，指向定义该值的那条 `Instruction`
- `ValueInfo::result_index` 用于区分多结果指令中的第几个结果；单结果指令固定为 `0`
- `TypeFact` 是 side data，不要求在 builder 阶段立即填满；允许先保留默认值，后续由类型分析回填

第一版推荐的 helper 语义：

```cpp
bool ValueTable::empty() const noexcept {
    return values.empty();
}

ValueInfo* ValueTable::find(ValueId value_id) noexcept {
    if (!value_id.is_valid() || value_id.value() >= values.size()) {
        return nullptr;
    }
    return &values[value_id.value()];
}
```

对应的 `const` 版本保持一致。

### 5.2.1 builder 写入时机

为了让 `ValueTable` 从 IR 构建阶段就保持自洽，当前按两步写入：

1. `create_value()`

- 分配新的 `ValueId`
- 同步向 `current_unit->value_table.values` 追加一个占位 `ValueInfo`
- 该占位项此时只填 `value_id`，`def == nullptr`，`result_index == 0`

2. `append_instruction()`

- 扫描这条指令产出的 `result` 或 `results`
- 回填对应 `ValueInfo.def`
- 多结果指令额外回填 `result_index`
- 只有当 builder 能直接确定事实时才写入 `TypeFact`，否则保留默认 `unknown`

这样有两个好处：

- `ValueId` 分配后立刻能在 `ValueTable` 中查到，便于后续分析阶段直接按 id 访问
- verifier 可以独立检查“值已分配”与“值已被某条指令定义”这两个条件

### 5.2.2 verifier 约束

引入 `ValueTable` 后，建议 verifier 增加以下检查：

- `ValueTable` 中每个 `ValueInfo.value_id` 都必须有效，且与其下标一致
- 若启用当前 builder 的稠密 id 假设，则 `values.size()` 应等于该 unit 中分配过的最大 `ValueId + 1`
- 每个被某条指令产出的 `ValueId` 都必须在 `ValueTable` 中存在对应项
- 每个 `ValueInfo.def` 若非空，必须属于当前 `CodeUnit`
- `ValueInfo.def` 必须真的产出对应的 `ValueId`
- 同一个 `ValueId` 只能由一条指令定义，这一约束仍以现有 verifier 的 `define_value` 规则为准，
  但应再与 `ValueTable.def` 交叉校验

### 5.2.3 第一版不做什么

第一版建议明确不把以下内容塞进 `ValueTable`：

- use-list
- reaching-def 链
- 跨 unit 的全局值编号
- 调用解析事实

这些信息要么维护成本高，要么不属于“`ValueId -> 元信息`”的核心职责。

### 5.3 `Slot` 只保留可选的类型约束

`SlotInfo` 仍然可以携带一份轻量“声明约束”或“运行时固定角色类型”，但它不应直接等同于当前值类型。

例如：

- lowering 内部的 `InternalLocal` 迭代下标可以声明为 `Int64Scalar`
- 以后若支持 builtin ABI 或参数注解，也可以把约束挂在 `Arg/Ret` slot 的 metadata 上

但 `load` 产生的 `ValueId` 类型，仍应由 reaching defs 或数据流分析得出。

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

## 7. 类型事实如何产生

### 7.1 构建期种子事实

IR builder 只写入不依赖数据流、不依赖名字解析、也不依赖 Matlab 动态分派的事实。

- `ConstInst` 直接给出精确类型事实
- `LoadSlotInst` 如果读取的 slot 带有 `SlotInfo::value_type` 约束，则直接使用该固定类型
- 已知 internal helper / primitive 可以写入保守摘要
- 其他结果值默认保持 `unknown`

例如：

- `Int64Constant -> int64`
- `Float64Constant -> double`
- `StringLiteralConstant -> string`
- `EmptyDoubleMatrixConstant`
  如果当前实现能明确表达空 double 矩阵，则可记录为 `double` 且非标量；否则保留 `unknown`
- `SlotInfo::value_type = Int64Scalar -> int64 scalar`
- `internal.foreach_init(iterable) -> (extern scalar, int64 scalar)`
- `internal.cmp_gt(lhs, rhs) -> logical scalar`
- `internal.add(lhs, rhs)`
  只有当左右操作数都已有类型事实，且 `TypeSet` 与 scalar 属性完全一致时，结果才继承
  该类型；否则保持 `unknown`

Matlab 源码中的普通数字字面量当前按 double 语义降低；例如 `.m` 文件里的 `1` 会生成
`Float64Constant`，用户可见 IR 打印为 `double`。

### 7.2 后续类型推导 pass

构建结束后，再由单独的类型推导 pass 逐步填充更多事实：

- 没有固定类型的 `LoadSlotInst` 可通过 slot reaching-def / 前向数据流获得类型事实
- `ScriptVar` slot 读取只有在脚本名字稳定区间分析能证明绑定或 reaching def 时才给出更强事实
- `UnaryInst / BinaryInst` 只有在静态分派或 builtin 语义可证明时才给出具体结果类型
- `ApplyInst / CallInst` 只有在名字解析和实参类型足够收敛时才给出具体结果类型

若某个 pass 已经分析过但无法继续收窄，才把结果设为 `Any`。如果 pass 尚未覆盖该值，
仍应保持 `unknown`。

第一版类型推导完全可以只支持最小数值规则，例如：

- `double + double -> double`
- `int64 + int64 -> int64`
- 两侧类型事实不一致的内部 `add -> unknown`
- 比较运算结果 -> `Logical`

### 7.3 slot 数据流

在 `FunctionUnit` 中：

- `StoreSlotInst(slot, value)` 更新该 slot 的当前抽象状态
- `LoadSlotInst(slot)` 读取该 slot 当前抽象状态并赋给结果 `ValueId`

本质上这是一套 non-SSA 前向数据流分析。

### 7.4 脚本 slot 读取

- `LoadSlotInst(ScriptVar)`
  构建阶段默认保持 `unknown`

除非后续单独做了脚本名字稳定区间分析，否则不应在这一层擅自给出更强类型结论。
如果类型推导 pass 已经确认无法稳定该脚本 slot 读取，才可把它设为 `Any`。

### 7.5 调用结果

- `ApplyInst`
  构建阶段默认保持 `unknown`
- `CallInst`
  动态分派的调用构建阶段默认保持 `unknown`；少数 internal helper 可以按内置摘要写入
  类型事实，例如 `internal.foreach_init`

后续 pass 若命中已知 builtin 摘要，且名字解析与实参类型足够稳定，才可根据实参类型生成更强结果。
否则，如果 pass 已经分析完仍无法收窄，结果可保守设为 `Any`。

## 8. 类型事实如何帮助分派

### 8.1 builtin 路径收敛

若名字解析已经稳定到 builtin 候选，参数类型事实可以继续决定：

- 是否可直接进入数值 fast path
- 是否必须走通用 helper
- 是否需要先检查复数、标量或 shape 条件

例如：

- `sin(a)` 且 `a : double`
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
- 若任一来源仍是 `unknown`，且当前 pass 没有足够信息补齐，则结果继续保持 `unknown`
- 差异过大但分析已经完成时，可退回 `Any`

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
- 一部分 slot 绑定或动态 env 相关假设可能需要回退
- 已缓存的分派结论可能只能在 guard 成立时复用

因此这里的类型事实设计应天然允许：

- 在事实失效且尚未重新分析时回退到 `unknown`
- 在分析确认只能给出全集上界时设为 `Any`
- 降级成更保守的类型结论
- 交给 IR 解释器继续执行

## 11. 与 typed SSA 的关系

这套设计不是 `typed SSA` 的替代品，而是它的前置信息来源之一。

更自然的关系是：

1. 主 `IR` 上先形成 `ValueId -> TypeFact`
2. 热点 region 提取时，筛出足够稳定的值和调用点
3. `typed SSA` 只消费其中可证明或可 guard 的部分
4. guard 失败时回退到对应 IR continuation

也就是说：

- 主 `IR` 构建后允许存在大量 `unknown`
- 类型分析之后仍可能存在大量保守 `Any`
- `typed SSA` 只吃最有价值、最稳定的那一部分类型事实

## 12. 第一阶段非目标

当前不建议一开始就做以下事情：

- 不把主 `IR` 改造成全局强类型表示
- 不要求每个 `ValueId` 都有精确类型
- 不要求完整 Matlab 类型格
- 不要求一次覆盖脚本 `ScriptVar` 绑定、`global`、`persistent`、closure
- 不要求立刻支持所有 builtin 的精细摘要

先把“函数内纯局部 slot + 少量 builtin + 基础数据流”打通，收益和复杂度比最高。

## 13. 建议落地顺序

当前已经落地的基础部分：

- `CodeUnit` 持有 `ValueTable`
- `create_value()` 同步创建占位 `ValueInfo`
- `append_instruction()` 回填 `def` 和 `result_index`
- `ConstInst` 写入基础常量类型事实

后续建议顺序：

1. 增加 `FunctionUnit` 内基于 slot 的前向类型数据流
2. 为少量数值 builtin 建立摘要表
3. 在调用点引入 `ResolutionFact + TypeFact` 的联合收敛逻辑
4. 再把这些事实接给 inline cache、baseline JIT 和后续 `typed SSA`

## 14. 当前阶段的一句结论

如果把本文压缩成一句设计判断，可以写成：

> 当前主 `IR` 不需要直接变成强类型 `IR`；更合适的做法是在 `ValueId` 上维护面向调用分派的类型事实，并将其与名字解析稳定性、epoch guard 和后续 `typed SSA` 明确分层。
