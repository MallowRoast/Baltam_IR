# IR Verifier 设计

## 目标

本文记录当前 `src/ir/ir_verify.{h,cpp}` 已经实现的第一版 verifier。

`IRVerifier` 的职责是检查已经构造完成的 IR 是否满足当前 schema 的结构不变量。它不做
Matlab 类型推断，也不尝试判定完整语言语义是否正确。

当前 verifier 主要服务于三类场景：

- `IRLowerer` 产物的端到端验证
- 后续 IR transform / pass 的输入输出检查
- smoke test 中尽早暴露 parent 指针、CFG 边、slot/value 引用等结构错误

## 公开接口

公开头文件：`src/ir/ir_verify.h`

当前接口如下：

```cpp
struct IRVerifyDiagnostic {
    enum Severity : std::uint8_t {
        Warning,
        Error,
    };

    Severity severity = Error;
    InternedString message;
    SourceSpan source_span;
};

struct IRVerifyOptions {
    bool require_terminated_blocks = true;
    bool require_dense_slot_ids = false;
};

struct IRVerifyResult {
    std::vector<IRVerifyDiagnostic> diagnostics;

    bool ok() const noexcept;
};

IRVerifyResult verify_ir(
    const IRModule& module,
    const IRVerifyOptions& options = {});

IRVerifyResult verify_ir(
    const MFileUnit& mfile,
    const IRVerifyOptions& options = {});
```

调用方优先对完整 `IRModule` 调用；需要文件级兼容入口时也可以验证 `MFileUnit`：

```cpp
const IRVerifyResult result = verify_ir(*module);
if (!result.ok()) {
    // 处理 Error 级诊断
}
```

## 诊断模型

verifier 使用独立的 `IRVerifyDiagnostic`，不复用 `IRBuildDiagnostic`。

这样做是为了保持职责边界清晰：

- `IRBuildDiagnostic`
  来自 builder / lowering 阶段，表示构建动作本身的问题
- `IRVerifyDiagnostic`
  来自 verifier 阶段，表示最终 IR 对象违反 schema 约束

`IRVerifyResult::ok()` 只关心是否存在 `Error` 级诊断。`Warning` 可用于 strict 检查或风格检查，
不阻止 IR 继续被打印或传给后续实验流程。

## 选项

### `require_terminated_blocks`

默认值为 `true`。

开启后，每个 `BasicBlock` 都必须以 `GotoInst`、`BranchInst` 或 `ReturnInst` 结束。

当前 lowering 已经会为没有显式 terminator 的 block 补隐式 `return`，因此默认采用 closed block
约束。后续如果需要验证 builder 构造中的中间态，可以关闭该选项。

### `require_dense_slot_ids`

默认值为 `false`。

开启后，verifier 会检查 `SlotId` 是否符合当前 builder 的 unit-local 递增分配规则。

该检查当前只产生 warning，因为 `SlotId` 的稠密性是 builder 实现策略，不是核心 IR schema 的
必要语义。后续如果 runtime frame layout 直接依赖稠密 `SlotId`，可以把这项升级为 error。

## 当前检查范围

### 1. `IRModule / MFileUnit`

verifier 会检查：

- `IRModule::files` 不能为空
- `IRModule::files` 中不能有空指针
- 每个 `MFileUnit::module` 必须指回所属 module
- module 级匿名函数表不能包含空函数体
- 匿名函数体 ID 必须有效且不能重复
- 匿名函数体 `lexical_parent` 必须属于当前 module
- `entry_unit` 不能为空
- `entry_unit` 必须属于当前 `MFileUnit::code_units`
- `code_units` 中不能有空指针
- `ScriptUnit::file` / `FunctionUnit::file` 必须指回当前文件
- `local_function_map` 的目标不能为空
- `local_function_map` 的目标必须属于当前文件
- `local_function_map` 的 key 必须与目标函数名一致

### 2. `CodeUnit`

verifier 会检查：

- `entry_block` 不能为空
- `entry_block` 必须属于当前 `CodeUnit::basic_blocks`
- `basic_blocks` 中不能有空指针
- 每个 `BasicBlock::parent` 必须指回当前 `CodeUnit`
- `type() == Script` 时对象必须是 `ScriptUnit`
- `type() == Function` 时对象必须是 `FunctionUnit`
- `type() == AnonymousFunction` 时对象必须是 `AnonymousFunctionUnit`

### 3. `FunctionUnit / AnonymousFunctionUnit`

verifier 会检查：

- `param_slots` 引用的 slot 必须存在
- `param_slots` 引用的 slot 必须是 `SlotTag::Arg`
- `param_slots` 内部不能重复引用同一个 slot
- `return_slots` 引用的 slot 必须存在
- `return_slots` 引用的 slot 必须是 `SlotTag::Ret`
- `return_slots` 内部不能重复引用同一个 slot
- 匿名函数体 `id` 必须有效
- 匿名函数体 `param_slots` 只能引用 `Arg` slot
- 匿名函数体 `capture_slots` 只能引用 `Capture` slot
- 匿名函数体参数 / 捕获列表内部不能重复

### 4. `SlotTable`

verifier 会检查：

- 每个 `Slot::id` 必须有效
- 同一个 `CodeUnit` 内 `Slot::id` 不能重复
- `SlotTag::ScriptVar` 只能出现在脚本单元中
- `SlotTag::Capture` 只能出现在匿名函数体单元中
- `Nargin / Nargout / Varargin / Varargout` 只能出现在 `FunctionUnit`
- `Nargin / Nargout / Varargin / Varargout` 在同一个 `CodeUnit` 中同一 tag 至多出现一次

### 5. `BasicBlock`

verifier 会检查：

- block 中不能包含空指令
- 每条指令的 `parent` 必须指回所在 block
- terminator 只能出现在 block 末尾
- 开启 `require_terminated_blocks` 时，block 必须以 terminator 结束

### 6. CFG 边

当前 IR 同时保存两类 CFG 状态：

- `BasicBlock::predecessors / successors`
- `GotoInst / BranchInst` 的目标块

verifier 会检查这两类状态一致：

- `successors` 中的 block 必须属于同一个 `CodeUnit`
- `predecessors` 中的 block 必须属于同一个 `CodeUnit`
- successor 的 `predecessors` 必须反向包含当前 block
- predecessor 的 `successors` 必须反向包含当前 block
- `successors` 必须包含 terminator 声明的所有目标
- `successors` 不能包含 terminator 未声明的目标
- `ReturnInst` 所在 block 不能有 successors

### 7. `ValueId`

verifier 会检查：

- 产生结果的指令必须使用有效 `ValueId`
- 同一个 `CodeUnit` 内 `ValueId` 只能被定义一次
- `ApplyInst::results` 内部不能重复
- `CallInst::results` 内部不能重复
- 多结果指令中的 `InvalidValueId` 表示占位输出位，verifier 会跳过该位
- `ValueId` 操作数必须引用当前 unit 中已经定义过的值

当前实现按指令遍历顺序检查 `ValueId` 使用。因此它也会捕获当前非 SSA IR 中不应出现的
use-before-def。

### 8. `Operand`

verifier 会检查：

- `Operand(ValueId)` 必须引用已定义值
- `Operand(Slot)` 必须引用当前 `CodeUnit` 中存在的 slot
- `Operand(InternedString)` 不能为空

这些检查覆盖：

- `StoreSlotInst::value`
- `ApplyInst::callee_or_base / arguments`
- `CallInst::callee / arguments`
- `UnaryInst::operand`
- `BinaryInst::lhs / rhs`
- `BranchInst::condition`
- `ReturnInst::values` 逐项检查为已定义 `ValueId`

### 9. 具体指令约束

verifier 会检查：

- `LoadSlotInst::slot` 必须存在
- `StoreSlotInst::slot` 必须存在
- `StoreSlotInst` 不能写入匿名函数体的 `Capture` slot
- `CreateAnonymousFunctionHandleInst::function_id` 必须能在所属 module 的匿名函数表中找到
- `CreateAnonymousFunctionHandleInst::captures` 的 `captured_value` 必须引用当前外层 unit
  中已定义的值
- 有效的 capture `source_slot` 必须属于当前外层 unit
- `GotoInst::target` 必须非空，且属于同一 `CodeUnit`
- `BranchInst::true_target / false_target` 必须非空，且属于同一 `CodeUnit`

`CallInst` 还会按 `callee_kind / dispatch_type` 做额外检查：

- `Direct`
  - `callee` 必须是非空 `InternedString`
- `Indirect`
  - 不能使用 `Internal / MFunction` 静态分派
  - `callee` 不能是 `InternedString`
  - `callee` 如果是 `ValueId` 或 `Slot`，必须满足普通 operand 引用规则
- `dispatch_type = Internal`
  - `callee_kind` 必须是 `Direct`
- `dispatch_type = MFunction`
  - `m_function_target` 必须非空
  - `m_function_target` 必须属于同一个 `MFileUnit`
  - `callee` 名字必须与 `m_function_target->name` 一致

## 当前不检查的范围

第一版 verifier 不检查：

- Matlab 类型、shape、标量/矩阵兼容性
- 运算符重载最终解析是否正确
- `ApplyInst` 最终应解释为函数调用还是索引访问
- 函数实参与形参数量是否匹配
- definite assignment
- 可达性
- 支配关系
- SSA 形式
- `EffectClass` 是否语义完全精确
- `InstAttrs::may_throw` 是否完整标注
- `SourceSpan` 是否落在真实源码文本范围内

这些检查要么需要类型系统，要么需要更完整的 Matlab 语义模型，要么更适合放在后续 pass 的
专用 verifier 中。

## 与 smoke test 的关系

当前 `test/smoke_test/smoke_test_common.h` 已经在 `require_ir_is_complete(...)` 中调用
`verify_ir(...)`。

这意味着 smoke test 会同时覆盖：

- lowering 是否成功
- verifier 是否接受当前 lowering 产物
- 各语法样例的核心 IR 结构、CFG target、slot/value 引用是否符合预期

syntax smoke test 不再检查文本 IR 显示格式。`ir_print` 的 CLI 可用性由单独的
`ir_print_cli_smoke` 覆盖，具体排版约定归 printer 自身维护。

## 后续演进方向

后续可以考虑：

- 增加 `verify_code_unit(...)` 级别的局部入口，方便 pass 单独验证一个函数
- 增加 source text 长度选项，用于检查 `SourceSpan` 边界
- 增加 `EffectClass` / `InstAttrs` 的 strict 检查
- 在引入 typed IR 或 SSA IR 后，为新层级增加独立 verifier
- 在 CFG 状态收敛为单一真源后，删除当前 predecessor/successor 与 terminator 目标的一致性检查
