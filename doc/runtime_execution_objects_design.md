# InterpreterContext、CodeObject 与 Frame 设计

本文定义 high-level IR 直接解释执行时的三个一级运行时对象：

```text
InterpreterContext
  ├── CodeObjectCache
  ├── AnonymousCodeTable
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
  owns BaseWorkspace / GlobalRegistry / code cache / anonymous code table
  non-owning current_frame / interrupt state

CodeObjectCache
  owns file-backed / named shared_ptr<CodeObject>

AnonymousCodeTable
  owns anonymous-function shared_ptr<CodeObject>

CodeObject
  shares ownership of IRModule
  non-owning pointer to one CodeUnit inside that module
  owns persistent value table / code revision metadata

Frame
  non-owning pointer to CodeObject
  owns per-call slot entries / ValueId temporaries / dynamic bindings
  non-owning caller and InterpreterContext pointers

ClosureObject
  shares ownership of CodeObject
  owns capture environment
```

`CodeObject` 发布后，其 `CodeUnit`、block、instruction、slot 和 value 定义必须保持不可变。
如果源码或优化结果变化，应创建新 revision，而不是原地修改活跃 Frame 正在执行的 IR。

## 2. InterpreterContext

`InterpreterContext` 是一次解释器会话的执行状态容器。它只保存解释执行期间必须共享、并且没有
更合适 owner 的状态：

- base workspace 和 global value table
- 具名 M 代码 cache
- 匿名函数体代码表
- 当前活跃 Frame 指针
- 宿主请求中断的标志

路径解析、source loader、builtin/plugin registry、诊断输出、debugger、trace、profile 和
pass pipeline 配置都不属于 `InterpreterContext`。这些能力由项目已有 runtime/host service 或
`CodeObject` 构建层提供。

```cpp
inline constexpr std::size_t kMaxCallDepth = 1024;

class InterpreterContext final {
public:
    BaseWorkspace base_workspace;
    GlobalRegistry globals;

    CodeObjectCache code_cache;
    AnonymousCodeTable anonymous_codes;

    Frame* current_frame = nullptr;

    std::atomic_bool interrupt_requested{false};
};
```

不放入 `InterpreterContext` 的状态：

| 内容 | 归属 |
|---|---|
| 函数解析、路径搜索、source 过期判断 | 项目已有符号查找 / source loader |
| builtin/plugin registry | 项目已有 runtime 分派机制 |
| source 过期状态、路径/代码失效状态 | 外层 invalidation 机制；通过 cache invalidate 接口生效 |
| verify、pass pipeline、构建选项 | `CodeObject` 构建层 |
| diagnostic、debugger、trace、profile | runtime/host service 或 side table |
| 当前调用深度计数器 | 创建 Frame 前从 caller 链计算，并检查 `kMaxCallDepth` |

### 2.1 `base_workspace`

```cpp
struct BaseWorkspace {
    std::unordered_map<InternedString, ba_obj_ptr> values;
};
```

它表示命令行、REPL 和顶层脚本使用的普通工作区。`values` 是按名字索引的 session 级变量存储，
不是某个 `CodeUnit` 的 `SlotId`。顶层脚本
不拥有独立变量存储，而是把 `ScriptVar` 访问映射到这里。

`values[name] == nullptr` 表示该名字当前未绑定或已 clear。base workspace 与 global table 是
两种存储；`global x` 只在当前 workspace 建立到 `GlobalRegistry` 的绑定，不把 global value
复制到 `values`。

### 2.2 `globals`

```cpp
struct GlobalRegistry {
    std::unordered_map<InternedString, ba_obj_ptr> values;
};
```

它保存 session 级共享 global value。声明了同名 global 的不同 Frame 访问同一个
`ba_obj_ptr`。

- `values[name] == nullptr` 表示该 global 当前未绑定或已 clear。
- 普通函数 Frame 不拥有 global table；但 `global x` 仍为 `x` 分配普通 `SlotId`。该 slot
  持有 `ba_obj_ptr`，并且与 `GlobalRegistry` 中同名 value 指向同一个变量地址，
  不把 global value 复制成一份独立局部值。

### 2.3 `code_cache`

```cpp
struct CodeCacheKey {
    NormalizedPath source_file;
    InternedString unit_name;
};

class CodeObjectCache final {
public:
    std::shared_ptr<CodeObject> find(const CodeCacheKey& key) const;

    void insert(CodeCacheKey key, std::shared_ptr<CodeObject> code);
    void invalidate(const CodeCacheKey& key);
    void invalidate_source(const NormalizedPath& source_file);
    void invalidate_all();
    void collect_retired(const Frame* current_frame);

private:
    std::unordered_map<CodeCacheKey, std::shared_ptr<CodeObject>> entries_;
    std::vector<std::shared_ptr<CodeObject>> retired_;
};
```

它缓存已经完成 parse、IR lowering、verify、pass pipeline 和代码级运行时状态初始化的
`CodeObject`。
缓存失效只阻止新调用使用旧对象。由于 `Frame::code` 是裸指针，`invalidate*` 不能直接释放旧
`CodeObject`；它应把旧对象标记 `invalidated` 后移入 `retired_`。Frame 退出后，解释器可以用
当前 `current_frame` 链扫描活跃调用栈，并通过 `collect_retired` 释放已不再被任何活跃 Frame 指向的
旧 revision。

`InterpreterContext` 不保存独立函数解析器，也不保存第二份 builtin/plugin registry。函数、脚本、
builtin 和 plugin 的名字查找继续使用项目已有符号查找与 runtime 分派机制；只有目标解析为可执行
M 代码时，才从 `code_cache` 取得或构建 `CodeObject`。

cache key 只包含规范化文件路径和文件内 unit 身份。这里暂用 `unit_name` 表达主函数、脚本顶层
unit 和 local function 的区别；如果 IR 后续提供稳定 `CodeUnitId`，可以替换这个字段。路径搜索、
builtin/plugin 优先级、class/private/local 函数解析都在进入 cache 前完成；cache 不重复保存
resolver 结果，也不判断源码内容是否过期。源码是否过期由外层 source loader、路径解析和
clear/rehash 机制判断，并触发对应 `invalidate_source` 或 `invalidate_all`。

`CodeObjectCache` 不保存匿名函数体。匿名函数没有 Matlab 函数名字空间中的可查找名字，应该进入
独立的 `anonymous_codes`。

### 2.4 `anonymous_codes`

```cpp
struct AnonymousCodeKey {
    const IRModule* module = nullptr;
    AnonymousFunctionId function_id = InvalidAnonymousFunctionId;
};

class AnonymousCodeTable final {
public:
    std::shared_ptr<CodeObject> find(const AnonymousCodeKey& key) const;
    void insert(AnonymousCodeKey key, std::shared_ptr<CodeObject> code);
    void invalidate_module(const IRModule* module);
    void invalidate_all();
    void collect_retired(const Frame* current_frame);

private:
    std::unordered_map<AnonymousCodeKey, std::shared_ptr<CodeObject>> entries_;
    std::vector<std::shared_ptr<CodeObject>> retired_;
};
```

它是 session 级匿名函数代码表，不是 Matlab `global` 变量表。key 使用 `IRModule` 身份和
`AnonymousFunctionId`，因为匿名函数 ID 只在所属 module 内唯一。

执行 `CreateAnonymousFunctionHandleInst` 时，解释器用当前 `CodeObject::ir_owner.get()` 和
指令中的 `function_id` 查找或构建匿名函数体 `CodeObject`，再把该 `CodeObject` 与捕获值一起放入
`ClosureObject`。匿名函数体不参与普通名字查找，也不进入 `CodeObjectCache`。

源码或 pipeline 失效时，对应 module 的匿名函数表项必须从 `anonymous_codes` 的 active entries
移入 `retired_`。已经构造好的 closure 可以继续通过 `shared_ptr<CodeObject>` 保持旧匿名函数体
存活；活跃匿名函数 Frame 则通过 `collect_retired(current_frame)` 的 caller 链检查避免裸指针悬空。

### 2.5 `current_frame`

它是非拥有指针，指向当前最内层活跃 Frame；没有执行中的函数时为 `nullptr`。

```text
current_frame -> caller -> caller -> nullptr
```

它用于错误栈、debugger、profiler、`evalin("caller", ...)` 和 `assignin("caller", ...)`。普通
函数局部变量查找不能沿 caller 链向上搜索。

进入和退出 Frame 应使用 RAII，保证 runtime error 或 C++ 异常展开时仍能恢复
`current_frame`。

调用深度上限使用 `kMaxCallDepth` 这样的编译期常量或构建配置，不作为 `InterpreterContext` 成员。
创建新 Frame 前从 caller 链计算当前深度即可。

### 2.6 `interrupt_requested`

宿主通过它请求中断执行。解释器至少在函数入口、循环回边和长时间 helper 返回后检查。中断
处理应转成统一 runtime error，再通过正常 Frame 展开恢复状态。

## 3. CodeObject

`CodeObject` 对应一个已经完成 lowering、verify 和 pass pipeline 的可执行 `CodeUnit`。它不再
复制 `CodeUnit` 中已有的 slot/value/source/profile/signature 元数据，只保存解释执行必须跨调用
共享的代码级状态：

```cpp
class CodeObject final {
public:
    std::shared_ptr<IRModule> ir_owner;
    CodeUnit* unit = nullptr;

    PersistentTable persistent;

    bool invalidated = false;
    std::uint64_t revision = 0;
};
```

不放入 `CodeObject` 的重复派生字段：

| 信息 | 来源 |
|---|---|
| slot 数量和 slot 名字 | `unit->slot_table` |
| ValueId 数量和 def 信息 | `unit->value_table` |
| 参数顺序 | `FunctionUnit::param_slots` / `AnonymousFunctionUnit::param_slots` |
| 返回值顺序 | `FunctionUnit::return_slots` |
| `varargin/varargout/nargin/nargout` | `SlotTag` |
| entry block | `unit->entry_block` |
| block 顺序 | `unit->basic_blocks` |
| 代码显示身份 / source range | `CodeUnit` / instruction 的 source span，或外层 AST/source manager |
| profile 统计 | profile side table |
| `contains_eval` 等 feature flag | 需要时扫描 IR 或由构建层 side table 提供 |

这样 `Frame` 创建时直接按 `SlotId` / `ValueId` 分配运行时数组，不需要额外的 frame/value/
signature/name 映射结构。

`CodeObject` 不保存额外代码身份结构。具名代码的 cache 身份在 `CodeCacheKey` 中，匿名函数体身份
在 `AnonymousCodeKey` 中；调用栈、debugger 和诊断显示名从 `CodeUnit`、instruction source span
或外层 AST/source manager 派生。

### 3.1 `ir_owner` 与 `unit`

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

### 3.2 直接索引策略

`SlotId` 和 `ValueId` 都直接作为当前 `Frame` 内数组索引使用：

```text
SlotId  -> Frame::slot_values[SlotId]
ValueId -> Frame::temporaries[ValueId]
```

Frame 创建时按以下大小分配：

```text
slot_values.size()  == unit->slot_table.slots.size()
temporaries.size()  == unit->value_table.values.size()
```

slot 的存储来源由 `SlotInfo::slot.tag` 决定，不需要单独的运行时 slot 映射表：

| Slot 类别 | 存储位置 |
|---|---|
| `Arg/Ret/Local/InternalLocal` | 当前 `Frame::slot_values[SlotId]` 拥有的 slot |
| `Varargin/Varargout` | 用户可见 Frame slot |
| `Capture` | `Frame::slot_values[SlotId]` 指向 closure capture 中的变量值 |
| `Persistent` | `Frame::slot_values[SlotId]` 指向 `CodeObject::persistent` 中的变量值 |
| `Global` | `Frame::slot_values[SlotId]` 指向 `InterpreterContext::globals` 中的同一变量地址 |
| `ScriptVar/BaseVar` | `Frame::slot_values[SlotId]` 指向 target workspace binding 的变量值 |
| `Nargin/Nargout` | Frame 调用元数据 |

普通 `LoadSlotInst/StoreSlotInst` 只读写 `Frame::slot_values[SlotId]`，不查哈希表，也不查额外映射表。
不同 `SlotTag` 的差异只体现在 Frame 初始化阶段：初始化时把 slot entry 连接到当前 Frame、global
binding、persistent value table、closure capture 或 workspace binding。

按名机制也不需要 `CodeObject` 维护第二份符号表。`eval`、脚本、`who/whos` 和 debugger 可以扫描
`unit->slot_table.slots`，或在解释器内部建立非语义缓存；缓存必须能从 `SlotTable` 重建，不能成为
新的语义来源。

### 3.3 `persistent`

```cpp
struct PersistentTable {
    std::unordered_map<SlotId, ba_obj_ptr> values;
};
```

`PersistentTable` 是当前 `CodeObject` 私有的 value table，形状与 `GlobalRegistry` 类似：
global 用变量名索引，persistent 用稳定的 `SlotId` 索引。准确说，`SlotId` 是这张表的 key，
表里的 value 仍然是 `ba_obj_ptr`。

persistent 属于代码级状态，递归和后续调用共享，不属于任何一次 Frame。Frame 初始化到
`SlotTag::Persistent` 时，把 `Frame::slot_values[SlotId]` 连接到
`CodeObject::persistent.values[SlotId]` 的同一变量地址。

`persistent.values[SlotId] == nullptr` 表示该 persistent 变量尚未初始化或已被 clear。因为
persistent 变量自己的 `SlotId` 在同一个 `CodeObject` revision 内稳定，所以不需要
额外 offset、`storage_index` 或与整个 slot table 等长的 storage vector。

### 3.4 `invalidated` 与 `revision`

`CodeObject` 构造成功即表示 IR 已通过 verifier、pass pipeline 已结束且 IR 已冻结，因此不再保存
构建状态位。新调用只需要检查 `!invalidated`。失效对象不再接受新调用，
但旧 Frame 可以继续通过裸指针执行它；因此拥有该 `CodeObject` 的 code table 必须把失效对象保留到
没有活跃 Frame 可能再引用它。`revision` 区分同一源码函数的多个代码版本，不等同于环境 epoch。

### 3.5 构建顺序

```text
parse / load AST
  -> IR lowering
  -> verify
  -> PassManager
  -> verify
  -> PersistentTable
  -> freeze CodeObject
  -> insert CodeObjectCache 或 AnonymousCodeTable
```

具名函数和脚本插入 `CodeObjectCache`；匿名函数体插入 `AnonymousCodeTable`。任何步骤失败都不能把
半初始化对象放入表中。

## 4. Frame

Frame 对应一次函数或匿名函数调用：

```cpp
struct Frame {
    InterpreterContext* context = nullptr;
    CodeObject* code = nullptr;
    Frame* caller = nullptr;

    BasicBlock* block = nullptr;
    std::uint32_t instruction_index = 0;

    std::vector<ba_obj_ptr> slot_values;
    std::vector<ba_obj_ptr> temporaries;

    std::uint32_t actual_nargin = 0;
    std::uint32_t requested_nargout = 0;

    std::shared_ptr<ClosureEnvironment> closure;
    std::unique_ptr<DynamicBindings> dynamic_bindings;
};
```

### 4.1 `context`、`code` 与 `caller`

- `context` 是非拥有指针，提供 global、base workspace、code cache、anonymous code table、
  current frame 和 interrupt state。
- `code` 是非拥有指针。`CodeObjectCache`、`AnonymousCodeTable` 或 closure 持有所有权；cache
  失效只能阻止新调用，不能销毁仍可能被活跃 Frame 指向的旧 CodeObject。
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

### 4.3 `slot_values`

`slot_values` 按 `code->unit->slot_table.slots.size()` 分配，每个 `SlotId` 都有对应 entry。对于
`Arg/Ret/Local/InternalLocal`，entry 是当前 Frame 拥有的变量槽；对于 `Global`、`Persistent`、
`Capture` 和 `Workspace`，entry 持有与对应外部存储一致的 `ba_obj_ptr`。

不再维护单独的 `slot_bound`。`slot_values[SlotId] == nullptr` 表示该变量当前未初始化或已被
`clear`；合法运行时值必须是非空 `ba_obj_ptr`。空矩阵、空字符串等语言值仍然是非空对象，不能用
nullptr 表示。

```text
load nullptr slot  -> cleared/undefined variable error
store slot         -> 写入非空 ba_obj_ptr
clear slot         -> slot value 置 nullptr
```

静态变量被 clear 后不能回退为同名函数解析。

### 4.4 `temporaries`

它保存当前调用中 `ValueId` 的运行时结果，按 `code->unit->value_table.values.size()` 分配。
普通 ValueId 访问直接使用 `Frame::temporaries[ValueId]`，不需要额外的 temporary 映射字段。
临时值不进入 name lookup，不被 `who/whos` 观察，也不与 SlotId 共用编号空间。

### 4.5 调用元数据

- `actual_nargin` 是调用者实际传入的参数数量。
- `requested_nargout` 是调用者请求的返回值数量。
- 参数和返回值顺序使用 `FunctionUnit::param_slots` / `FunctionUnit::return_slots`，匿名函数参数
  使用 `AnonymousFunctionUnit::param_slots`。
- `varargin/varargout` 是用户可见变量，存放在对应 `SlotTag::Varargin` / `SlotTag::Varargout`
  的 Frame slot 中。

### 4.6 `closure`

匿名函数调用时，它指向构造句柄时按值保存的 capture environment。`ClosureObject` 同时保存从
`AnonymousCodeTable` 得到的匿名函数体 `CodeObject`。`Capture` slot 通过 `SlotId` 读取 closure
environment 中对应的 `ba_obj_ptr`。closure 不引用外层 Frame，因此不会延长外层调用生命期。

### 4.7 `dynamic_bindings`

只有执行 `eval`、脚本、`assignin` 或运行时创建动态名字时才延迟分配。静态名字仍由
`unit->slot_table` 定位到 slot；不属于静态 slot 表的名字存入这里。

## 5. 调用流程

### 5.1 进入函数

```text
1. 普通 M 函数通过项目已有符号查找机制和 `CodeObjectCache` 得到 `CodeObject`；匿名函数通过
   closure 中保存的 `CodeObject` 调用；builtin/plugin 转交既有分派机制。
2. 检查 invalidated，并确认 caller 链深度不超过 `kMaxCallDepth`。
3. 创建 Frame，设置 context/code/caller。
4. 按 `CodeUnit` 的 slot/value table 分配 slot values 和 temporaries。
5. 按 `CodeUnit` 的接口 slot 写入实参并构造 varargin。
6. 设置 actual_nargin/requested_nargout。
7. continuation 指向 `unit->entry_block`。
8. 通过 RAII 更新 current_frame。
9. 开始解释执行。
```

### 5.2 返回函数

```text
1. 按 `FunctionUnit::return_slots` 检查并收集命名返回值。
2. 按请求数量展开 varargout。
3. 恢复 InterpreterContext::current_frame。
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
每个静态可见源码变量都有 SlotId，Frame 中有对应 slot entry。
local/arg/ret slot entry 由 Frame 拥有。
global/persistent/capture/workspace slot entry 指向对应外部存储的同一变量地址。
capture 属于 ClosureEnvironment。
ValueId 结果属于 Frame::temporaries。

CodeObject 发布后 IR 不可变。
匿名函数体 CodeObject 属于 InterpreterContext::anonymous_codes，不属于 CodeObjectCache。
Frame 不拥有 caller 或 InterpreterContext。
Frame 不拥有 CodeObject；CodeObject owner 必须保证活跃 Frame 指针不悬空。
普通函数名字查找不沿 caller 链进行。
builtin/plugin 继续使用项目已有机制。
```

## 相关文档

- [IR Schema](./ir_schema.md)
- [M 工作区设计](./workspace_design.md)
- [M 变量模型设计](./variable_model_design.md)
- [Global / Persistent IR 节点设计](./global_persistent_ir_design.md)
- [Matlab 执行策略与 Runtime 机制设计](./execution_strategy.md)
