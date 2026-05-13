# 匿名函数句柄设计问题记录

本文记录匿名函数句柄 `@(args) expr` 的 IR 设计问题。当前先明确语义边界和第一阶段 IR
形状，后续实现时再按代码结构细化字段名。

## 讨论目标

匿名函数句柄需要同时表达三件事：

- 匿名函数体的代码：例如 `@(x) x + y` 中的 `x + y`
- 构造句柄时的捕获：运行到 `f = @(x) x + y` 时捕获当前 `y` 的值
- 调用句柄时的分派：`f(1)` 应先读取变量 `f` 的运行时值，再由 runtime 判断它是不是函数句柄

这三件事不应该混在一个静态名字 lookup 机制里。

## 当前设计倾向

第一阶段按以下方向落地：

- 每个匿名函数体分配一个 `AnonymousFunctionId`，形式类似 `SlotId` / `ValueId`
- `AnonymousFunctionId` 在一次 IR module / build session 内全局唯一
- 匿名函数体使用新的 `AnonymousFunctionUnit` 表示，并考虑继承 `CodeUnit`
- 匿名函数体不进入 Matlab 函数名字 lookup 表
- 匿名函数没有显式返回参数；匿名函数体直接返回表达式值
- 捕获在构造点保存的是外层 `ValueId` 对应的运行时值
- 匿名函数体内部通过自己的 capture slot 读取捕获值，产生 body-local `ValueId`
- 全局匿名函数表拥有 `AnonymousFunctionUnit`，普通 IR 指令只通过 `AnonymousFunctionId`
  间接引用匿名函数体

这里的“全局”建议先理解为 IR module / build session 级别，而不是进程级静态全局状态。这样
测试、并行 lowering 和后续多文件编译都更容易控制生命周期。

## 已确认语义

### 1. 匿名函数捕获当前值

```matlab
y = 1;
f = @(x) x + y;
y = 10;
z = f(2);
```

`f` 中看到的 `y` 应是构造 `f` 时的值，即 `1`。因此 `z` 应为 `3`。

先不考虑 handle object / Java object 等引用语义对象时，可以把捕获视为值快照：

- 构造匿名函数句柄时，对自由变量执行一次读取
- 将读取出的值放进 closure capture 环境
- 后续外层变量重新赋值不影响已构造的匿名函数句柄

### 2. 匿名函数体不能修改捕获绑定

MATLAB 匿名函数体只能是表达式，不能在函数体内直接写赋值语句。因此先按只读捕获处理：

- captured variable 在匿名函数体中只能读取
- 不允许对 captured slot 生成 `store_slot`

### 3. 匿名函数没有显式返回参数

源码层 `@(x) x + y` 的函数体只能是表达式，不声明返回参数名。因此 IR 中不应给匿名函数
体建立 `return_slots`，也不应引入合成的 `ans` 返回 slot 作为语义必需结构。

匿名函数体直接返回表达式 lowering 后的 `ValueId`：

```ir
anon #anon0(%slot0 @x) captures (%slot1 @y) {
entry:
  %0 = load_slot %slot0
  %1 = load_slot %slot1
  %2 = add %0, %1
  ret %2
}
```

如果后续执行层需要 ABI 临时返回槽，可以在 bytecode / runtime frame 布局阶段添加，不放进
语义 IR 的匿名函数接口里。

### 4. `f()` 的目标解析是运行时事情

`f(1)` 在 lowering 阶段不应该尝试“找到匿名函数体”。

lowering 只应做：

```ir
%f0 = load_slot %slot_f
%r0 = value_apply %f0(%arg0)
```

runtime 执行 `value_apply` 时，再根据 `%f0` 的运行时值分派：

- 如果是匿名函数句柄，从句柄对象中取出 function body/code 和 captures
- 如果是具名函数句柄，按句柄内的绑定或 unresolved 名字规则调用
- 如果是数组或对象，按下标访问 / overload 规则继续分派

## 与具名函数句柄的区别

`@sin` 和 `@(x) x + y` 的核心语义不同。

具名函数句柄：

- 保存名字 `sin`
- 构造时可能进行一次函数 lookup
- 如果构造时查到目标，后续句柄固定指向该目标
- 如果构造时没查到，后续调用时继续 lookup

匿名函数句柄：

- 没有 Matlab 名字空间里的函数名
- 构造时创建 closure 实例
- closure 实例携带函数体代码引用和捕获值
- 后续调用完全通过运行时句柄对象进入

因此匿名函数不应该复用 `CreateNamedFunctionHandleInst`。

## 当前倾向的 lowering 形状

源码：

```matlab
y = 1;
f = @(x) x + y;
z = f(2);
```

外层 lowering 可以形如：

```ir
%0 = const 1
store_slot %slot_y, %0

%1 = load_slot %slot_y
%2 = create_anon_func #anon0 captures { %slot_y }
store_slot %slot_f, %2

%3 = load_slot %slot_f
%4 = const 2
%5 = value_apply %3(%4)
store_slot %slot_z, %5
```

匿名函数体 lowering 可以形如：

```ir
anon #anon0(%slot0 @x) captures (%slot1 @y) {
entry:
  %0 = load_slot %slot0
  %1 = load_slot %slot1
  %2 = add %0, %1
  ret %2
}
```

这里的 `#anon0` 是 IR/runtime 内部代码对象 ID，不是 Matlab 名字空间里的函数名。

## 捕获值到函数体 ValueId 的链路

捕获变量在构造匿名函数时已经固定。以 `y` 为例，外层 lowering 应先读取外层 slot：

```ir
%1 = load_slot %slot_y
%2 = create_anon_func #anon0 captures { %slot_y }
```

这里 `%1` 是外层 `CodeUnit` 中的 `ValueId`。运行到 `create_anon_func` 时，runtime 把 `%1`
对应的运行时值保存进 closure capture environment：

```text
closure.code = #anon0
closure.captures[y] = runtime_value(%1)
```

匿名函数体内部不能直接引用外层 `%1`。`ValueId` 只在所属 `CodeUnit` 内有效；跨过匿名函
数边界的是 runtime value，不是 IR `ValueId`。

匿名函数体拥有自己的 capture slot：

```ir
anon #anon0(%slot0 @x) captures (%slot1 @y) {
entry:
  %1 = load_slot %slot1
  ...
}
```

每次调用 closure 时，runtime 创建匿名函数调用 frame，并把 closure 中已经固定的捕获值填
入匿名函数 frame 的 capture slot：

```text
frame.slot0 = call_argument_0
frame.slot1 = closure.captures[y]
```

随后 body 内的 `load_slot %slot1` 产生匿名函数体自己的 body-local `ValueId`。

完整链路是：

```text
外层 source SlotId
  -> 构造点 load_slot
  -> 外层 ValueId
  -> create_anon_func 保存运行时值
  -> closure capture value
  -> 调用时填入匿名函数 frame 的 capture SlotId
  -> body 内 load_slot
  -> body-local ValueId
```

因此，`create_anon_func` 的 capture 记录里应保存外层构造点的 `ValueId`；匿名函数体的 slot
表里应保存对应的 capture slot。二者通过 `AnonymousFunctionId` 和 capture 顺序 / 名字建立
映射，但不是同一个实体。

### 函数和脚本中的捕获来源

函数 / 匿名函数体内的变量已经静态绑定到 slot，因此捕获来源是变量自己的 `SlotId`：

```ir
%1 = load_slot %slot_y
%2 = create_anon_func #anon0 captures { %slot_y }
```

脚本中的变量来自当前 workspace，而不是函数 frame 里的 local slot。因此脚本里捕获自由变
量时，lowering 应根据当前 outer unit 是 `ScriptUnit` 生成 `load_workspace`：

```ir
%1 = load_env %slot_env, @y
%2 = create_anon_func #anon0 captures { @y }
```

这里 `@y` 只表示捕获来源是脚本 workspace 中名为 `y` 的变量；真正被 closure 捕获的仍然
是 `%1` 对应的运行时值快照，后续脚本中 `y` 再赋值不影响已创建的匿名函数句柄。

`CreateAnonymousFunctionHandleInst::CaptureValue` 中可以共用一个 `source_slot` 字段：

- outer unit 是函数 / 匿名函数体时，`source_slot` 是被捕获变量自己的静态 slot，
  `captured_value` 由 `load_slot source_slot` 产生
- outer unit 是脚本时，`source_slot` 是脚本的 WorkspaceHandle hidden slot，
  `captured_value` 由 `load_workspace source_slot, name` 产生

因此 `name` 不需要再复制一份 `workspace_symbol`。在脚本捕获中，`name` 同时就是
`load_workspace` 使用的 workspace symbol，也是匿名函数体 capture slot 的名字。

## Capture load 开销与后续优化

语义 IR 中保留 `load_slot %capture_y`，是为了清晰表达“这个 body-local 值来自 closure
capture”。这不意味着最终执行时必须付出昂贵的动态查找成本。

后续可以分层优化：

- capture slot layout：把 capture slot 编译成 closure env 的固定 index / offset
- entry hoisting：只读 capture slot 可以在 entry block 读取一次，后续复用同一个 `ValueId`
- load forwarding / CSE：同一 capture slot 的重复读取可以合并
- capture-to-SSA lowering：执行 IR / bytecode 阶段把 capture slot 转成隐式参数或 SSA 输入
- specialized closure call：若 call site 能证明目标是某个匿名函数，可把 captures 作为已知值传入
  并进一步内联

第一阶段不建议在语义 IR 中用跨 `CodeUnit` 的 `ValueId` 直接替代 body 内的 capture
`load_slot`。这样会破坏 `ValueId` 的作用域边界，也会让 verifier 和后续优化更难维护。

### load_slot 降级前的局部规约

`load_slot` / `store_slot` 仍应先保留在语义 IR 中。真正把 slot 访问降成 frame offset、
workspace lookup 或 closure capture offset 之前，先运行一个轻量的 slot canonicalization pass。
这个 pass 位于：

```text
AST lowering
  -> semantic IR verify
  -> slot canonicalization
  -> semantic IR verify
  -> load_slot / store_slot lowering
  -> bytecode / backend lowering
```

slot canonicalization 先做两个局部 pass：

1. `store_slot` + `load_slot` forwarding

   当同一个 basic block 中出现连续指令：

   ```ir
   store_slot %slot_y, %0
   %1 = load_slot %slot_y
   ```

   且中间没有任何指令时，`%1` 可以直接替换为 `%0`，随后删除该 `load_slot`：

   ```ir
   store_slot %slot_y, %0
   ; users of %1 now use %0
   ```

   这个 pass 适合最早运行，因为它只依赖局部连续指令关系，不需要复杂别名分析。匿名函数捕
   获点会因此从：

   ```ir
   store_slot %slot_y, %0
   %1 = load_slot %slot_y
   %2 = create_anon_func #anon0 captures { %slot_y }
   ```

   规约为：

   ```ir
   store_slot %slot_y, %0
   %2 = create_anon_func #anon0 captures { %slot_y }
   ```

   这样 `create_anon_func` 捕获的仍是构造点值，只是不再通过一次立即读回的
   `load_slot` 形成新 `ValueId`。

2. 保守 dead store elimination

   forwarding 之后再做 DSE。最小安全规则是：同一个 basic block 内，如果前一个
   `store_slot %slot_y, V0` 到下一个 `store_slot %slot_y, V1` 之间没有：

   - `load_slot %slot_y`
   - 可能读取 workspace / frame 的指令
   - 可能让 slot 状态被观察到的未知调用
   - `eval` / `assignin` / 动态 workspace 相关指令
   - 控制流边界

   则前一个 store 可以删除。

   对匿名函数捕获示例，forwarding 后如果 `y = 1` 的 slot 状态在 `y = 10` 前没有被观
   察到，则：

   ```ir
   %0 = const 1
   store_slot %slot_y, %0
   %2 = create_anon_func #anon0 captures { %slot_y }
   %3 = const 10
   store_slot %slot_y, %3
   ```

   可以进一步删除第一个 `store_slot %slot_y, %0`：

   ```ir
   %0 = const 1
   %2 = create_anon_func #anon0 captures { %slot_y }
   %3 = const 10
   store_slot %slot_y, %3
   ```

   这里删除的是 slot 写入，不是 `%0` 的值本身；`create_anon_func` 仍然持有 `%0` 作为
   捕获值 operand，因此匿名函数的值捕获语义不变。

这两个 pass 不应放进匿名函数 lowering 本身。匿名函数 lowering 只负责生成语义正确的
`load_slot` 和 `create_anon_func`；slot canonicalization 负责消掉局部冗余；后面的
`load_slot` / `store_slot` 降级 pass 再处理仍然保留下来的真实 slot 访问。

## 已暴露的设计问题

### 1. 匿名函数体什么时候 lowering

匿名函数体应在 IR lowering 阶段处理，而不是运行时再解析源码。

当 lowering 遇到 `node_anonymous_func` 时：

- 分析参数列表
- 分析函数体表达式
- 识别自由变量
- 生成匿名函数体 IR
- 在外层构造点生成 `create_anon_func` 指令，并把捕获值作为 operands

运行时只负责创建 closure 对象和执行 closure code，不负责重新 lowering。

### 2. 匿名函数体放在哪里

当前倾向是增加全局匿名函数表：

```text
AnonymousFunctionTable
  #anon0 -> AnonymousFunctionUnit
  #anon1 -> AnonymousFunctionUnit
```

全局表拥有匿名函数体，普通 IR 只保存 `AnonymousFunctionId`。这样可以：

- 方便 lowering 期间分配匿名函数 id
- 方便 printer / verifier 遍历
- 方便跨 MFile 传递匿名函数句柄
- 避免 IR 指令直接拥有或强持有匿名函数体

`MFileUnit` 或 `FunctionUnit` 可以保存辅助索引，用于记录本文件 / 本函数内创建过哪些匿名函
数。但第一阶段的所有权建议先放在全局匿名函数表中。

后续可以基于逃逸分析改进存储位置：

- 可能作为返回值、写入 workspace/global/persistent、传给未知调用或放入逃逸容器的匿名函数，
  保留在全局匿名函数表
- 能证明只在当前 M 函数内部使用的匿名函数，可以放入对应 M 函数的局部匿名函数表

这个优化只改变编译期组织和生命周期管理，不改变匿名函数 ID、capture 语义和调用语义。

### 3. 跨 MFile 调用问题

匿名函数句柄可以作为返回值逃逸：

```matlab
function f = make(y)
    f = @(x) x + y;
end
```

其他文件拿到 `make(10)` 返回的句柄后仍应能调用它。

因此 runtime 句柄不能只保存一个裸 `AnonymousFunctionId`，因为这个 id 如果只在当前文件内唯一，跨文件时无法单独定位代码。

### 4. 不应让句柄强持有整个 MFile

如果 runtime 句柄保存的是：

```text
module/MFile + anonymous_function_id + captures
```

那么复制匿名函数句柄可能导致整个 MFile/runtime module 不能释放。这会把“代码归属”和“生命周期归属”绑死，设计上不理想。

更合理的是：匿名函数句柄持有可执行的 closure code object，而不是持有整个 MFile。

## 候选 runtime 模型

编译期可以仍然有匿名函数表，但 codegen/runtime 阶段应把匿名函数体拆成独立代码对象：

```cpp
struct RuntimeAnonymousFunctionHandle {
    std::shared_ptr<const ClosureCode> code;
    CaptureEnv captures;
};
```

复制匿名函数句柄时：

- 复制 `ClosureCode` 引用
- 复制或共享捕获环境
- 不强持有整个 MFile

`ClosureCode` 可以显式保存自己真正需要的依赖，而不是间接 pin 住整个文件：

```cpp
struct ClosureCode {
    AnonymousFunctionId id;
    // lowered body / bytecode / native code
    // explicit dependencies if needed
};
```

这样：

- 文件级 IR 表仍可作为编译期组织结构
- runtime closure code 可独立存活
- 最后一个引用该 closure code 的句柄释放后，closure code 才释放

## IR 层可能需要的结构

后续可以考虑新增：

```cpp
struct AnonymousFunctionIdTag;
using AnonymousFunctionId = EntityId<AnonymousFunctionIdTag>;

struct AnonymousFunctionUnit : CodeUnit {
    AnonymousFunctionId id = AnonymousFunctionId::invalid();
    std::vector<SlotId> param_slots;
    std::vector<SlotId> capture_slots;

    [[nodiscard]] Type type() const noexcept override {
        return CodeUnit::AnonymousFunction;
    }
};

struct AnonymousFunctionTable {
    std::vector<std::unique_ptr<AnonymousFunctionUnit>> functions;
};

class CreateAnonymousFunctionHandleInst final : public Instruction {
public:
    ValueId result = InvalidValueId;
    AnonymousFunctionId function_id = AnonymousFunctionId::invalid();

    struct CaptureValue {
        InternedString name;
        // 函数 / 匿名函数体中是被捕获变量自己的 slot；
        // 脚本中是 WorkspaceHandle hidden slot，name 作为 workspace symbol。
        SlotId source_slot = InvalidSlotId;
        ValueId captured_value = InvalidValueId;
    };
    std::vector<CaptureValue> captures;
};
```

这里 `source_slot` 主要用于诊断、分析和后续优化；真正表达捕获值的是
`captured_value`。匿名函数体内部对应的 capture slot 保存在 `AnonymousFunctionUnit::capture_slots`
和 slot table 中。

## Verifier 约束草案

无论最终结构如何，以下约束大概率成立：

- 匿名函数体不进入 Matlab 函数名字 lookup 表
- `lookup_method` 不应返回匿名函数体
- `create_anon_func` 的结果类型固定为 `function_handle scalar`
- `create_anon_func` 的 `function_id` 必须能在匿名函数表中解析到 `AnonymousFunctionUnit`
- `create_anon_func` 的 capture operands 必须是当前外层 `CodeUnit` 中已定义的 `ValueId`
- capture 的 `source_slot` 如果有效，必须属于当前外层 `CodeUnit`
- 匿名函数体的 capture slot 必须属于对应 `AnonymousFunctionUnit`
- 匿名函数体中的 captured slot 只读
- 匿名函数体没有显式 `return_slots`
- `value_apply` 不静态假设 base 是匿名函数句柄，除非后续分析证明

## 待定问题

- `CodeUnit::Type` 是否直接新增 `AnonymousFunction`，还是用独立继承层辅助标记
- 捕获变量在 body 内用 captured slot 表示，还是增加 `LoadCaptureInst`
- 自由变量分析由 lowering 临时完成，还是先建立独立 AST 分析 pass
- 捕获值是深拷贝、COW value，还是 runtime 对 `ba_obj` 的现有复制语义
- 匿名函数体引用 local function / private function / import 时，runtime closure code 如何保存依赖
- closure code object 的序列化、缓存和释放策略
- 逃逸分析如何证明匿名函数体可以放入 M 函数局部表，而不是全局匿名函数表

## 当前结论

当前最稳妥的方向是：

- lowering 阶段遇到匿名函数表达式时，立即 lower 匿名函数体
- 为匿名函数体分配全局唯一 `AnonymousFunctionId`
- 匿名函数体用 `AnonymousFunctionUnit` 表达，并由全局匿名函数表拥有
- 匿名函数没有显式返回参数，body 直接 `ret` 表达式结果值
- 外层只生成构造 closure 的指令，捕获值作为外层 `ValueId` operands
- 匿名函数体内部通过自己的只读 capture slot `load_slot` 得到 body-local `ValueId`
- `f()` 仍 lower 成 `load f` + `value_apply`
- runtime function handle 持有独立 closure code object 和 captures
- 不让 runtime handle 强持有整个 MFile/module
- 后续通过 capture slot layout、capture-to-SSA、specialized call 和内联消解 capture `load_slot`
  的执行开销
