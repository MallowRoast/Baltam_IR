# InterpreterContext、CodeObject 与 Frame 设计

本文定义 high-level IR 直接解释执行时的三个一级运行时对象：

```text
InterpreterContext
  └── CodeObject cache
        └── Frame -> caller Frame -> ...
```

它们的职责边界是：

- `InterpreterContext` 保存一次解释器会话共享的全局状态。
- `CodeObject` 保存一个优化完成且已经冻结的 `CodeUnit` 及其代码级元数据。
- `Frame` 保存执行该 `CodeObject` 的一次函数调用状态。

解释器直接执行 high-level IR，不在 `CodeUnit` 与 interpreter 之间增加另一套执行指令。
项目已有机制继续负责 builtin 和 plugin 的查询与调用，这三个对象不重复维护 builtin 注册表。

## 1. 总体所有权

```text
InterpreterContext
  owns BaseWorkspace / GlobalRegistry / resolver / code cache
  non-owning current_frame

CodeObjectCache
  owns shared_ptr<CodeObject>

CodeObject
  shares ownership of IRModule
  non-owning pointer to one CodeUnit inside that module
  owns layouts / persistent cells / code metadata

Frame
  shares ownership of CodeObject
  owns per-call slots / ValueId temporaries / dynamic bindings
  non-owning caller and InterpreterContext pointers

ClosureObject
  shares ownership of CodeObject
  owns capture environment
```

`CodeObject` 发布后，其 `CodeUnit`、block、instruction、slot 和 value 定义必须保持不可变。
如果源码或优化结果变化，应创建新 revision，而不是原地修改活跃 Frame 正在执行的 IR。

## 2. InterpreterContext

建议第一版按单线程解释执行设计：

```cpp
class InterpreterContext final {
public:
    BaseWorkspace base_workspace;
    GlobalRegistry globals;

    FunctionResolver resolver;
    CodeObjectCache code_cache;

    Frame* current_frame = nullptr;
    std::size_t call_depth = 0;

    EnvironmentEpochs epochs;
    RuntimeOptions options;

    DiagnosticEngine diagnostics;
    RuntimeServices services;

    std::atomic_bool interrupt_requested{false};
};
```

### 2.1 `base_workspace`

```cpp
struct WorkspaceBinding {
    ba_obj_ptr value;
    bool bound = false;
};

struct BaseWorkspace {
    std::unordered_map<InternedString, WorkspaceBinding> bindings;
    std::uint64_t version = 0;
};
```

它表示命令行、REPL 和顶层脚本使用的普通工作区。顶层脚本不拥有独立变量存储，而是把
`ScriptVar` 访问映射到这里。

`version` 在新建、删除或改变可观察 binding 时递增，供 workspace cache、debugger 和后续
guard 使用。base workspace 与 global table 是两种存储；`global x` 只在当前 workspace 建立
到 `GlobalRegistry` 的绑定，不把 global value 复制到 `bindings`。

### 2.2 `globals`

```cpp
struct GlobalCell {
    ba_obj_ptr value;
    bool bound = false;
    std::uint64_t version = 0;
};

struct GlobalRegistry {
    std::unordered_map<InternedString, GlobalCell> cells;
    std::uint64_t version = 0;
};
```

它保存 session 级共享 global cell。声明了同名 global 的不同 Frame 访问同一个 `GlobalCell`。

- `GlobalCell::version` 跟踪特定 cell 的值或绑定变化。
- `GlobalRegistry::version` 跟踪表结构变化，例如新增、删除或 `clear global`。
- 普通函数 Frame 不复制 global value，也不拥有 global cell。

### 2.3 `resolver`

```cpp
class FunctionResolver {
public:
    ResolveResult resolve_m_function(
        InterpreterContext& context,
        const ResolveRequest& request);

private:
    std::filesystem::path current_directory_;
    std::vector<std::filesystem::path> search_paths_;
};
```

它负责项目自身的 M 代码解析，包括当前文件 local function、private function、M 文件、当前
目录和搜索路径。解析失败或目标属于 builtin/plugin 时，调用流程交给已有 runtime 分派机制；
`InterpreterContext` 不保存第二份 builtin registry。

resolver cache 必须记录相关 `path` 或 `code` epoch，不能在 `cd`、路径变化、文件失效后继续
复用旧目标。

### 2.4 `code_cache`

```cpp
struct CodeCacheKey {
    NormalizedPath source_file;
    InternedString function_name;
};

struct CodeCacheEntry {
    std::shared_ptr<CodeObject> code;
    FileFingerprint source_fingerprint;
    std::uint64_t path_epoch = 0;
    std::uint64_t code_epoch = 0;
};
```

它缓存已经完成 parse、IR lowering、verify、pass pipeline 和 layout 构建的 `CodeObject`。
缓存失效只阻止新调用使用旧对象；活跃 Frame 通过 `shared_ptr` 保持旧 revision 存活。

cache key 至少包含规范化文件路径和函数/unit 身份。有效性需要考虑源码 fingerprint、路径环境、
IR pipeline 配置和 `clear functions`。

### 2.5 `current_frame`

它是非拥有指针，指向当前最内层活跃 Frame；没有执行中的函数时为 `nullptr`。

```text
current_frame -> caller -> caller -> nullptr
```

它用于错误栈、debugger、profiler、`evalin("caller", ...)` 和 `assignin("caller", ...)`。普通
函数局部变量查找不能沿 caller 链向上搜索。

进入和退出 Frame 应使用 RAII，保证 runtime error 或 C++ 异常展开时仍能恢复
`current_frame` 和 `call_depth`。

### 2.6 `call_depth`

它记录当前调用深度，用于递归限制、诊断和 profile。创建新 Frame 前检查：

```cpp
call_depth < options.max_call_depth
```

正常进入时递增，所有退出路径上递减。

### 2.7 `epochs`

```cpp
struct EnvironmentEpochs {
    std::uint64_t workspace = 0;
    std::uint64_t global = 0;
    std::uint64_t path = 0;
    std::uint64_t code = 0;
};
```

- `workspace`：base workspace、脚本、`eval`、`assignin` 或 `load` 改变可观察名字绑定。
- `global`：global registry 结构变化。
- `path`：`addpath`、`rmpath`、`cd`、`rehash` 等改变 M 函数解析环境。
- `code`：`clear functions`、文件失效或 IR pipeline revision 变化。

局部 Frame slot 的普通写入不推进全局 workspace epoch。特定 global cell 的普通值更新优先推进
cell version，不必让整个 global registry cache 失效。

### 2.8 `options`

```cpp
struct RuntimeOptions {
    std::size_t max_call_depth = 1024;
    bool verify_ir_before_execution = true;
    bool run_optimization_passes = true;
    bool collect_profile = false;
    bool enable_debugger = false;
    bool trace_calls = false;
    bool trace_instructions = false;
};
```

改变会影响 IR 或 layout 的 option 后，必须使相关 code cache 失效。trace 选项只服务诊断，不能
改变语言语义。

### 2.9 `diagnostics`

它统一产生 runtime error、warning、源码位置和调用栈。解释器和 helper 不应直接向
`std::cerr` 输出用户诊断。`DiagnosticEngine` 不拥有 Frame；需要调用栈时临时遍历
`current_frame`。

### 2.10 `services`

```cpp
struct RuntimeServices {
    OutputSink* output = nullptr;
    InputProvider* input = nullptr;
    FileSystem* filesystem = nullptr;
    Clock* clock = nullptr;
};
```

这些是 runtime 与宿主环境的非拥有接口，便于 CLI、GUI 和测试注入不同实现。第一版可以只接入
实际需要的 output 和 filesystem。

### 2.11 `interrupt_requested`

宿主通过它请求中断执行。解释器至少在函数入口、循环回边和长时间 helper 返回后检查。中断
处理应转成统一 runtime error，再通过正常 Frame 展开恢复状态。

## 3. CodeObject

`CodeObject` 对应一个可执行 `CodeUnit`，由所有调用共享：

```cpp
class CodeObject final {
public:
    CodeIdentity identity;

    std::shared_ptr<IRModule> ir_owner;
    CodeUnit* unit = nullptr;

    FrameLayout frame_layout;
    ValueLayout value_layout;
    FunctionSignature signature;

    NameBindingTable name_bindings;
    PersistentStorage persistent;

    ExecutionMetadata execution;
    SourceMetadata source;

    CodeObjectFlags flags;
    std::uint64_t revision = 0;
};
```

### 3.1 `identity`

```cpp
struct CodeIdentity {
    NormalizedPath source_file;
    InternedString function_name;
    CodeUnit::Type unit_type = CodeUnit::Function;
    AnonymousFunctionId anonymous_id = InvalidAnonymousFunctionId;
};
```

它是 cache、调用栈、debugger 和失效操作使用的代码身份。匿名函数使用 module 内 ID 作为真实
身份，内部文本名只用于显示。

### 3.2 `ir_owner` 与 `unit`

`ir_owner` 共享拥有完整 `IRModule`，`unit` 指向其中当前 CodeObject 执行的代码单元。不能只从
module 中拆出一个 `CodeUnit`，因为当前 IR 包含 file/module、local target、lexical parent、CFG
和 `ValueInfo::def` 等跨对象指针。

CodeObject 发布前必须满足：

```text
unit != nullptr
unit 由 ir_owner 间接拥有
IR 已通过 verifier
选定 pass pipeline 已结束
IR 已冻结，不再运行改写 pass
```

### 3.3 `frame_layout`

```cpp
enum class RuntimeStorageKind : std::uint8_t {
    FrameSlot,
    Capture,
    Persistent,
    Global,
    Workspace,
};

struct SlotRuntimeInfo {
    RuntimeStorageKind storage;
    SlotTag tag;
    std::uint32_t storage_index;
    InternedString name;
    bool user_visible = false;
    bool clearable = false;
};

struct FrameLayout {
    std::vector<SlotRuntimeInfo> slots;
    std::uint32_t frame_slot_count = 0;
    std::uint32_t user_binding_count = 0;
};
```

`slots[SlotId]` 把 IR slot 身份映射到运行时存储：

| Slot 类别 | 存储位置 |
|---|---|
| `Arg/Ret/Local/InternalLocal` | `Frame::slot_values` |
| `Varargin/Varargout` | 用户可见 Frame slot |
| `Capture` | closure capture environment |
| `Persistent` | `CodeObject::persistent` |
| `Global` | `InterpreterContext::globals` |
| `ScriptVar/BaseVar` | target workspace |
| `Nargin/Nargout` | Frame 调用元数据 |

`storage_index` 只在对应 storage kind 内有效，不能把所有 `SlotId` 直接当作 Frame offset。

### 3.4 `value_layout`

```cpp
struct ValueRuntimeInfo {
    std::uint32_t temporary_offset = InvalidRuntimeOffset;
};

struct ValueLayout {
    std::vector<ValueRuntimeInfo> values;
    std::uint32_t temporary_count = 0;
};
```

它把仍有定义的 `ValueId` 紧凑映射到 `Frame::temporaries`。pass 删除且 `def == nullptr` 的 value
不分配空间。Slot 和 ValueId 必须使用不同存储：slot 是用户变量或内部局部状态，ValueId 是 IR
数据流结果。

### 3.5 `signature`

```cpp
struct FunctionSignature {
    std::vector<std::uint32_t> parameter_slots;
    std::vector<std::uint32_t> return_slots;
    std::optional<std::uint32_t> varargin_slot;
    std::optional<std::uint32_t> varargout_slot;
};
```

这些字段保存运行时 Frame offset，不再保存原始 `SlotId`。它们决定参数初始化、命名返回值收集
以及 `varargin/varargout` 打包和展开顺序。`nargin/nargout` 保存在 Frame header，不进入用户
名字表。

### 3.6 `name_bindings`

它把用户可见静态名字映射到 storage kind 和 index，只给 `eval`、脚本、`who/whos`、debugger
等按名机制使用。普通 `LoadSlotInst/StoreSlotInst` 直接通过 `frame_layout` 访问，不查哈希表。
`InternalLocal`、ValueId 和隐藏调用状态不进入该表。

### 3.7 `persistent`

```cpp
struct PersistentCell {
    ba_obj_ptr value;
    bool initialized = false;
    std::uint64_t version = 0;
};

struct PersistentStorage {
    std::vector<PersistentCell> cells;
};
```

persistent 属于代码级状态，递归和后续调用共享，不属于任何一次 Frame。第一版单线程不需要在
每个 storage 内预放 mutex；真正支持并发重入时再确定同步策略。

### 3.8 `execution`

```cpp
struct InstructionLocation {
    BasicBlock* block = nullptr;
    std::uint32_t instruction_index = 0;
};

struct ExecutionMetadata {
    InstructionLocation entry;
    std::vector<BasicBlock*> block_order;
    std::unordered_map<BasicBlock*, std::uint32_t> block_ids;
    std::shared_ptr<CodeProfile> profile;
};
```

`entry` 是新 Frame 的初始 IR continuation。稳定 block ID 用于 profile、debug 和后续优化状态
映射，不依赖 block label 唯一。profile 是 side table，不能为了计数修改冻结的 IR。

### 3.9 `source`

它保存源码路径、函数范围、文件 fingerprint，以及可选 AST owner。解释执行只依赖 IR；AST
只服务源码诊断、debug 或重新构建，可以为空。正常执行不能回退到 AST 求值，否则 IR 不再是
唯一语义基线。

### 3.10 `flags` 与 `revision`

```cpp
struct CodeObjectFlags {
    bool verified = false;
    bool optimized = false;
    bool frozen = false;
    bool may_use_dynamic_workspace = false;
    bool contains_eval = false;
    bool contains_script_call = false;
    bool is_invalidated = false;
};
```

新调用只能使用 `verified && frozen && !is_invalidated` 的对象。失效对象不再接受新调用，但旧
Frame 可以继续持有它。`revision` 区分同一源码函数的多个代码版本，不等同于环境 epoch。

### 3.11 构建顺序

```text
parse / load AST
  -> IR lowering
  -> verify
  -> PassManager
  -> verify
  -> FrameLayout
  -> ValueLayout
  -> FunctionSignature / NameBindingTable / PersistentStorage
  -> ExecutionMetadata
  -> freeze CodeObject
  -> insert CodeObjectCache
```

任何步骤失败都不能把半初始化对象放入 cache。

## 4. Frame

Frame 对应一次函数或匿名函数调用：

```cpp
struct Frame {
    InterpreterContext* context = nullptr;
    std::shared_ptr<CodeObject> code;
    Frame* caller = nullptr;

    BasicBlock* block = nullptr;
    std::uint32_t instruction_index = 0;

    std::vector<ba_obj_ptr> slot_values;
    std::vector<std::uint8_t> slot_bound;
    std::vector<ba_obj_ptr> temporaries;

    std::uint32_t actual_nargin = 0;
    std::uint32_t requested_nargout = 0;

    std::shared_ptr<ClosureEnvironment> closure;
    std::unique_ptr<DynamicBindings> dynamic_bindings;
};
```

### 4.1 `context`、`code` 与 `caller`

- `context` 是非拥有指针，提供 global、base workspace、resolver、诊断和服务。
- `code` 共享拥有当前 CodeObject，保证 cache 失效时活跃执行仍安全。
- `caller` 是非拥有调用链指针，只服务调用栈和 caller workspace 选择，不参与普通局部查找。

### 4.2 IR continuation

`block + instruction_index` 表示下一条要执行的 high-level IR 指令：

```text
普通指令：instruction_index++
goto：block = target, instruction_index = 0
branch：选择 target, instruction_index = 0
return：结束当前 Frame
```

解释器直接从 `code->unit` 取得 block 和 instruction，不复制另一套指令。

### 4.3 `slot_values` 与 `slot_bound`

`slot_values` 只为 `FrameSlot` 类别分配，大小由 `frame_layout.frame_slot_count` 决定。
`slot_bound` 显式区分未初始化/已 clear 与合法 runtime value。

```text
load unbound slot  -> cleared/undefined variable error
store slot         -> 写 value 并置 bound
clear slot         -> 释放 value 并清 bound
```

静态变量被 clear 后不能回退为同名函数解析。

### 4.4 `temporaries`

它保存当前调用中 `ValueId` 的运行时结果，大小由 `value_layout.temporary_count` 决定。临时值
不进入 name lookup，不被 `who/whos` 观察，也不与 SlotId 共用编号空间。

### 4.5 调用元数据

- `actual_nargin` 是调用者实际传入的参数数量。
- `requested_nargout` 是调用者请求的返回值数量。
- `varargin/varargout` 是用户可见变量，存放在 signature 指定的普通 Frame slot 中。

### 4.6 `closure`

匿名函数调用时，它指向构造句柄时按值保存的 capture environment。`Capture` slot 通过
`SlotRuntimeInfo` 读取其中固定 index。closure 不引用外层 Frame，因此不会延长外层调用生命期。

### 4.7 `dynamic_bindings`

只有执行 `eval`、脚本、`assignin` 或运行时创建动态名字时才延迟分配。静态名字仍由
`CodeObject::name_bindings` 定位到 slot/persistent/global；不属于静态 layout 的名字存入这里。

## 5. 调用流程

### 5.1 进入函数

```text
1. resolver/code cache 得到 CodeObject。
2. 检查 verified、frozen、invalidated 和递归深度。
3. 创建 Frame，设置 context/code/caller。
4. 按 layout 分配 slot、binding state 和 temporaries。
5. 按 signature 写入实参并构造 varargin。
6. 设置 actual_nargin/requested_nargout。
7. continuation 指向 execution.entry。
8. 通过 RAII 更新 current_frame 和 call_depth。
9. 开始解释执行。
```

### 5.2 返回函数

```text
1. 按 return_slots 检查并收集命名返回值。
2. 按请求数量展开 varargout。
3. 恢复 InterpreterContext::current_frame 和 call_depth。
4. 把结果写入 caller 中 CallInst 对应的 ValueId temporary。
5. 析构当前 Frame 的 slot、temporary 和动态 binding。
```

所有 runtime error 和宿主异常路径都必须执行第 3、5 步。

## 6. 核心不变量

```text
会话级状态属于 InterpreterContext。
代码级共享状态属于 CodeObject。
调用级状态属于 Frame。

global 属于 InterpreterContext。
persistent 属于 CodeObject。
local/arg/ret 属于 Frame。
capture 属于 ClosureEnvironment。
ValueId 结果属于 Frame::temporaries。

CodeObject 发布后 IR 不可变。
Frame 不拥有 caller 或 InterpreterContext。
Frame 共享拥有 CodeObject。
普通函数名字查找不沿 caller 链进行。
builtin/plugin 继续使用项目已有机制。
```

## 相关文档

- [IR Schema](./ir_schema.md)
- [M 工作区设计](./workspace_design.md)
- [M 变量模型设计](./variable_model_design.md)
- [Global / Persistent IR 节点设计](./global_persistent_ir_design.md)
- [Matlab 执行策略与 Runtime 机制设计](./execution_strategy.md)
