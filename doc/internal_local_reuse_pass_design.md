# InternalLocal 复用 Pass 设计

## 目标

`InternalLocalReusePass` 是后续 IR cleanup / frame layout 前的内部临时 slot 复用 pass。

它的目标不是改变 Matlab 源码语义，也不是让基础 lowering 输出更“聪明”，而是在 lowering
已经生成清晰 canonical IR 之后，减少仅供 lowering/runtime 使用的内部 frame slot 数量。

典型动机来自短路逻辑：

```matlab
c = a && b;
d = a || e;
if c || d
    ...
end
```

当前 lowering 会为每个短路表达式创建一个 `InternalLocal` logical 结果 slot，例如
`__sc_and` / `__sc_or`。这些 slot 只用来在两条 CFG 路径汇合时承接结果；如果两个短路表达式的
live range 不重叠，它们理论上可以复用同一个物理 frame 位置。

## 非目标

- 不在 `IRLowerer` 里做复用。lowering 阶段仍优先输出稳定、可读、可验证的语义形状。
- 不复用用户可见 slot，包括 `Local`、`Arg`、`Ret`、`ScriptVar`、`Capture`。
- 不跨 `CodeUnit` 复用 slot。
- 第一版不处理 `global`、`persistent`、closure、动态 env 或脚本 workspace。
- 第一版不尝试删除或合并循环 carried state，例如 `__for_idx`。这类 slot 的 live range
  跨循环迭代，和短路临时结果不是同一类问题。

## Pass 位置

推荐 pipeline：

```text
AST
  -> IRLowerer
  -> canonical high-level IR
  -> optional cleanup passes
  -> InternalLocalReusePass
  -> runtime frame layout
```

第一阶段不建议让 `ir_print` 默认跑该 pass。文本 IR 保持 canonical lowering 形状更适合调试。
需要观察复用结果时，可以提供单独的 pass runner 或 `ir_print --run-pass=internal-local-reuse`
这类选项。

## 候选 slot

第一版建议只处理满足全部条件的 slot：

- `SlotTag::InternalLocal`
- `SlotValueType::LogicalScalar`
- 名字为 `__sc_and`、`__sc_or` 或后续统一出的 `__sc`
- 在当前 `CodeUnit` 内只被 `LoadSlotInst` / `StoreSlotInst` 引用
- 没有出现在函数签名、capture 列表、return 列表或外部 metadata 中

这些条件有意保守。等 pass 基础设施稳定后，再扩展到其他 lowering 内部临时 slot。

## Live Range 定义

对每个候选 slot，pass 需要计算：

- `defs`：所有 `StoreSlotInst` 写入位置
- `uses`：所有 `LoadSlotInst` 读取位置
- `live-in(block)` / `live-out(block)`：slot 在 CFG block 边界上的活跃状态
- `value_type`：slot 的固定值类型，用于避免不同类型复用

短路表达式的典型范围：

```text
sc.and.false:
  store %slot_sc, false
  br sc.and.end

sc.and.rhs:
  store %slot_sc, rhs
  br sc.and.end

sc.and.end:
  %v = load %slot_sc
```

这个 slot 在两个 store 前不 live，在 merge block 的 load 后结束 live。后续另一个短路表达式若在
该 load 之后才开始写自己的结果 slot，就可以复用。

## 正确性规则

两个 slot 可以复用，当且仅当：

1. 它们属于同一个 `CodeUnit`。
2. `SlotTag` 和 `SlotValueType` 相同。
3. 二者的 CFG live range 不相交。
4. 两者都不属于函数 ABI、用户名字、capture 或动态环境可观察状态。
5. 二者之间不存在需要同时保留两个内部值的路径。

不能只按打印顺序判断，因为 printer layout 不是 CFG 语义。必须基于 CFG predecessor /
successor 和指令位置计算活跃性。

## 实现策略

### 方案 A：只产出物理 frame 分配表

这个方案不改写 high-level IR：

```cpp
struct PhysicalSlotAssignment {
    Slot logical_slot;
    std::uint32_t physical_index;
};
```

runtime frame layout 使用该表把多个 logical `InternalLocal` 映射到同一个物理 frame
位置。

优点：

- 不改变 IR，风险最低。
- 不需要删除 slot table 条目。
- 文本 IR 仍保留清晰的 lowering 结果。

缺点：

- `ir_print` 仍会显示多个 logical internal slot。
- 复用效果只在 runtime frame layout 层可见。

### 方案 B：改写 IR slot 引用并清理 slot table

这个方案会把后续 slot 的 `LoadSlotInst` / `StoreSlotInst` 改写到 canonical slot：

```text
%slot4 @__sc_and
%slot6 @__sc_or
%slot8 @__sc_or

=> 所有可共用位置改写为 %slot4
```

随后从 `slot_table` 中删除已经没有引用的 `InternalLocal` slot。

优点：

- `ir_print` 可以直接看到更少的 slot。
- 后续 pass 看到的 IR 更紧凑。

缺点：

- 需要维护所有 slot 引用和 slot table。
- 如果开启 dense `SlotId` 检查，删除 slot 后可能需要重新编号并重写所有引用。
- 对调试不如 canonical lowering 形状直观。

第一阶段更推荐方案 A；方案 B 可以等 slot remap / def-use 基础设施完善后再做。

## 第一阶段范围

第一阶段建议实现为：

```cpp
class InternalLocalReusePass {
public:
    InternalLocalReuseResult run(CodeUnit& unit);
};
```

其中 `InternalLocalReuseResult` 只描述 logical slot 到 physical frame slot 的映射，不修改 IR。

最小功能：

- 扫描当前 `CodeUnit` 的 `InternalLocal logical` 短路 slot。
- 基于 CFG 计算 live range。
- 对不相交 live range 做线性扫描分配。
- 输出每个候选 slot 对应的 physical internal slot index。
- verifier 仍验证原始 high-level IR；runtime 使用复用结果时再做 frame layout 检查。

## 与短路 lowering 的关系

短路 lowering 仍保持：

- 每个 `&& / ||` 表达式创建一个 logical `InternalLocal` slot。
- CFG 显式表达 rhs 是否按需求值。
- merge block 通过 `load` 读出该表达式结果。

`InternalLocalReusePass` 只在后续阶段决定这些 logical slot 是否共享同一个物理 frame 位置。
这样可以同时保留 lowering 的可读性和执行层的紧凑性。

## 后续扩展

- 扩展到其他 lowering 内部临时 slot。
- 在 slot remap 基础设施完善后支持 IR 改写模式。
- 和 `FunctionFrameLayout` 对接，把复用结果直接变成 frame offset。
- 在 frame layout 构建后增加检查，确保两个映射到同一物理位置的 logical slot live range
  不重叠。
