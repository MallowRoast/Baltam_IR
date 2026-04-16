# Baltam_IR IR 设计说明

## 当前结论

当前仓库只保留一套正式 IR 定义：

- [src/ir/ir.h](/home/zj/Desktop/Baltam_IR/src/ir/ir.h)
- [src/ir/ir.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir.cpp)

这套 IR 已经同时承载：

- `NonSSA`
- `UntypedSSA`

旧 hybrid/value-based IR 已不再参与当前主线。

## IR 分层

当前类型层次如下：

- `IRNode`
  - 所有阶段共享的公共基类
- `NonSSANode`
  - non-SSA 节点基类
- `SSANode`
  - SSA 节点公共基类
- `UntypedSSANode`
  - 当前已落地的 SSA 节点基类

`IRNode` 当前保留的公共信息是：

- `stage`
- `parent`
- `source_location`

当前 `stage` 可能取值：

- `NonSSA`
- `UntypedSSA`
- `TypedSSA`

其中当前实际启用的是前两种。

## 当前两个已启用的 stage

### 1. `NonSSA`

当前 `NonSSA` 使用：

- `NamedValue`

来表示源码变量和 lowering 临时量。

它的特点是：

- 同一个名字可以多次定义
- 节点是线性语句式的
- 是 AST lowering 的直接输出

当前 non-SSA 节点集合包括：

- `NumberNode`
- `TextNode`
- `AssignNode`
- `UnaryOpNode`
- `BinOpNode`
- `CallNode`
- `CondJumpNode`
- `JumpNode`
- `ReturnNode`

### 2. `UntypedSSA`

当前 `UntypedSSA` 使用：

- `ValueId`
- `ValueRef`

来表示显式值流。

它的特点是：

- 每个 SSA 结果有独立定义点
- `phi` 独立放在块头
- 是当前统一的 SSA 打印和执行对象

当前 untyped SSA 节点集合包括：

- `SSANumberNode`
- `SSATextNode`
- `SSAUndefNode`
- `SSAPhiNode`
- `SSACopyNode`
- `SSAUnaryOpNode`
- `SSABinOpNode`
- `SSACallNode`
- `SSACondJumpNode`
- `SSAJumpNode`
- `SSAReturnNode`

## CFG 容器

当前容器层仍然统一为：

- `Module`
- `Function`
- `BasicBlock`

这三层在 `NonSSA` 和 `UntypedSSA` 之间直接复用。

### `BasicBlock`

当前 `BasicBlock` 持有：

- `phi_nodes`
- `instructions`
- `terminal`
- `predecessors`
- `successors`

约束：

- `phi` 只能出现在 `phi_nodes()`
- terminator 只能出现在 `terminal()`
- CFG 边必须双向一致

### `Function`

当前 `Function` 持有：

- 名字
- 类型
- 输入和输出签名
- `stage`
- SSA 参数槽位与 value table
- 基本块存储
- 节点存储
- 入口块

### `Module`

当前 `Module` 持有：

- 模块名
- 源文件路径
- 模块类型
- 函数列表
- 入口函数

## 当前打印语义

当前打印器位于：

- [src/ir/ir_printer.cpp](/home/zj/Desktop/Baltam_IR/src/ir/ir_printer.cpp)

打印风格仍然接近 LLVM IR，但现在已经同时支持：

- non-SSA IR
- untyped SSA IR

当前会输出：

- module / function 头
- block label
- predecessor / successor 注释
- 对齐后的源码注释
- SSA 值名和 `phi` 信息

## 当前阶段边界

当前职责划分已经明确：

- [src/lowering/lowering.cpp](/home/zj/Desktop/Baltam_IR/src/lowering/lowering.cpp)
  - 只负责 `AST -> NonSSA`
- [src/optimizer/construct_untyped_ssa.cpp](/home/zj/Desktop/Baltam_IR/src/optimizer/construct_untyped_ssa.cpp)
  - 负责 `NonSSA -> UntypedSSA`
- [src/interpreter/interpreter.cpp](/home/zj/Desktop/Baltam_IR/src/interpreter/interpreter.cpp)
  - 当前只执行 `UntypedSSA`

这意味着当前 lowering 不负责：

- SSA rename
- `phi` 插入
- type specialization
- LLVM lowering

## 上下文相关伪表达式

`end` 不应长期被视为普通 helper call。

当前实现里，AST 的 `node_magic_end` 会先被 lowering 成：

- non-SSA `CallNode(Direct, "magic_end", ...)`
- untyped SSA `SSACallNode::Callee::Direct("magic_end")`

这只是当前实现里的临时编码，不应当视为最终 IR 设计。

原因是 `end` 的绑定目标依赖“最近一层圆括号应用最终到底被解释成索引，还是函数调用”。

例如：

- `A(floor(end))`

这里的 `floor` 既可能是函数，也可能是一个标量 / 数组值：

- 如果 `floor` 是函数，`end` 应绑定外层 `A(...)`
- 如果 `floor` 是值，`floor(end)` 本身就是一层索引，`end` 应绑定内层 `floor(...)`

因此，不能仅靠 lowering 时看到的语法外形，就把这里的 `end` 永久固化成普通 direct call：

- `magic_end(A, index_position, total_index_count)`

更合理的长期方案是：

- 在 IR 里把 `magic_end` 设计成专门的 `IRNode`
- 它应保留“当前仍在等待绑定哪一层索引上下文”的语义
- 它不应被等同于普通内建函数名查找

换句话说，`magic_end` 是一种上下文相关伪表达式，而不是普通函数调用。

## `A(...)` / `call` / `magic_end` 的重设计

真正要重新设计的核心不是单独的 `magic_end`，而是当前 `CallNode` / `SSACallNode` 混合承载了两种不同层次的语义：

- 源码层的圆括号应用语法 `A(...)`
- 已经确认是“函数调用”的调用语义

这两者不应再共用同一个节点。

更合理的切分是：

- `ApplyNode` / `SSAApplyNode`
  - 表示源码里的通用圆括号应用 `A(...)`
  - 运行时再区分这是函数调用还是圆括号取子块
- `CallNode` / `SSACallNode`
  - 只表示已经确认是函数调用的语义
  - 不再承载“也许是 `block get`”这类歧义
- `MagicEndNode` / `SSAMagicEndNode`
  - 绑定到 `ApplySiteId`
  - 依赖的是“哪一层 `Apply` 最终被判成索引”，而不是“哪一层 `Call`”

### 1. non-SSA

建议把 non-SSA 的表达式位置 `A(...)` 统一 lower 成：

- `ApplyNode`

而不是当前这种：

- `CallNode(Direct/Indirect, ...)`

建议形状：

```cpp
using ApplySiteId = std::uint32_t;

class ApplyNode final : public NonSSANode {
public:
    struct Head {
        enum Type {
            DirectName,
            IndirectValue,
        };

        Type type = DirectName;
        std::string direct_name;
        NamedValue indirect_value;
    };

    ApplyNode(ApplySiteId site_id, Head head, std::vector<NamedValue> outputs,
              std::vector<NamedValue> inputs,
              std::optional<SourceLocation> location = std::nullopt);

    ApplySiteId site_id() const;
    const Head& head() const;
    const std::vector<NamedValue>& outputs() const;
    const std::vector<NamedValue>& inputs() const;
};
```

这里的 `ApplyNode` 表示的只是：

- 这是一层来自源码的 `(...)`
- head 要么是一个直接名字
- 要么是一个运行时值

但它还不是“已经确认的函数调用”。

`ApplyNode::Head` 的具体选择规则应是：

- `DirectName`
  - 只用于 head 是裸名字，且 lowering 没有把它识别成当前作用域里的值
  - 例如 `sin(x)`、`foo(x)`
- `IndirectValue`
  - 只要 head 已经是一个值，就统一走这里
  - 例如 `A(i)` 里 `A` 是已知局部/全局变量
  - 例如 `S.a(i)`、`C{1}(i)`、`f()(i)` 这类 head 不是裸名字的情况

也就是说，`IndirectValue` 这里的“indirect”不是“间接调用”的意思，而是：

- 当前这层 `A(...)` 的 head 已经被 lower 成一个普通值
- 至于这个值最终被当作函数句柄还是数组/对象 base，要等 `Apply` 自己执行时再决定

可以把它理解成：

```cpp
ApplyNode::Head lower_apply_head(const ast_ptr& head_ast, LoweringContext& ctx) {
    if (head_ast is bare_name && !should_treat_symref_as_user_value(...)) {
        return Head{DirectName, name, {}};
    }

    NamedValue head_value = lower_expr_to_operand(head_ast, ctx);
    return Head{IndirectValue, "", head_value};
}
```

因此对源码 `node_multiple_func` 的 lowering 规则应改成：

- 表达式位置的源码 `A(...)` 一律 lower 成 `ApplyNode`
- 不再直接生成 `CallNode`
- `CallNode` 只保留给：
  - lowering 主动插入的 helper/internal call
  - 已被后续 pass 证明为函数调用的 `ApplyNode`

在这个切分下，`CallNode` 自身也应重设计为“纯函数调用节点”：

```cpp
class CallNode final : public NonSSANode {
public:
    struct DirectCallee {
        enum Kind {
            Builtin,
            Internal,
            MFunction,
        };

        Kind kind = Builtin;
        std::string symbol;
    };

    struct Callee {
        enum Type {
            Direct,
            Indirect,
        };

        Type type = Direct;
        DirectCallee direct;
        NamedValue indirect_value;
    };

    CallNode(Callee callee, std::vector<NamedValue> outputs, std::vector<NamedValue> inputs,
             std::optional<SourceLocation> location = std::nullopt);

    const Callee& callee() const;
    const std::vector<NamedValue>& outputs() const;
    const std::vector<NamedValue>& inputs() const;
};
```

这里 direct call 需要显式区分：

- `Builtin`
  - 如 `sin`、`sqrt`、`getfield`
- `Internal`
  - 如 `__ir_make_cell__`、`__ir_paren_set__`、`foreach_iterate`
- `MFunction`
  - 模块内 primary/local function，或后续可链接的 M 函数

如果后续需要更细，`MFunction` 还可以再拆：

- primary function
- local function

但这一层可以晚一点再做。

间接调用则只表示：

- callee 是一个运行时值
- 预期它在执行时应当是函数句柄

也就是说：

- `ApplyNode` 负责承载源码 `A(...)`
- `CallNode` 负责承载已确认的函数调用

这两个职责必须拆开。

`CallNode` 的具体使用边界应是：

- `CallNode(Direct, Builtin, "sin")`
  - 表示“确定是 builtin 调用”
- `CallNode(Direct, Internal, "__ir_paren_set__")`
  - 表示“确定是 lowering helper/internal 调用”
- `CallNode(Direct, MFunction, "foo")`
  - 表示“确定是模块函数调用”
- `CallNode(Indirect, callee_value)`
  - 表示“确定是对函数句柄值的调用”
  - 如果运行时发现 `callee_value` 不是函数句柄，应直接报错

这里要强调：

- `ApplyNode(IndirectValue, value, ...)`
  - 值不是函数句柄时，可以合法解释成 `block get`
- `CallNode(Indirect, value, ...)`
  - 值不是函数句柄时，必须报错

两者不能混淆。

### 2. `magic_end`

在这个设计下，`magic_end` 不再依赖 `CallNode`，而应绑定 `ApplyNode` 的 `site_id`。

建议形状：

```cpp
struct EndBindingCandidate {
    ApplySiteId site_id;
    std::size_t index_position;
    std::size_t total_index_count;
};

class MagicEndNode final : public NonSSANode {
public:
    MagicEndNode(NamedValue result, std::vector<EndBindingCandidate> candidates,
                 std::optional<SourceLocation> location = std::nullopt);

    const NamedValue& result() const;
    const std::vector<EndBindingCandidate>& candidates() const;
};
```

例如 `A(floor(end))` 中，`MagicEndNode` 记录的是候选绑定：

- `floor(...)` 这一层 `ApplySiteId`
- `A(...)` 这一层 `ApplySiteId`

执行时谁先被判成“索引应用”，`end` 就绑定到谁。

`node_magic_end` 的 lowering 不应再像现在这样通过 AST 回溯，直接产出：

- `magic_end(base, index_position, total_index_count)`

更合理的是在 lowering context 里维护一条“当前正在 lowering 的 apply 输入栈”：

```cpp
struct ActiveApplyInput {
    ApplySiteId site_id;
    std::size_t index_position;
    std::size_t total_index_count;
};

struct LoweringContext {
    std::vector<ActiveApplyInput> active_apply_inputs;
    ApplySiteId next_apply_site_id = 1;
};
```

当 lowering 一条 `ApplyNode(site_id, ..., inputs)` 时：

1. 先分配 `site_id`
2. 对第 `i` 个输入做 lowering 前，push：
   - `{site_id, i, total_input_count}`
3. lower 该输入表达式
4. lower 完后再 pop

于是 `node_magic_end` 的 lowering 就变成：

```cpp
void lower_magic_end(const ast_ptr& node, const NamedValue& target, LoweringContext& ctx) {
    if (ctx.active_apply_inputs.empty()) {
        throw ...; // `end` 不在任何 apply 输入里
    }

    std::vector<EndBindingCandidate> candidates;
    candidates.reserve(ctx.active_apply_inputs.size());

    for (auto it = ctx.active_apply_inputs.rbegin(); it != ctx.active_apply_inputs.rend(); ++it) {
        candidates.push_back({it->site_id, it->index_position, it->total_index_count});
    }

    ctx.append_node<MagicEndNode>(target, std::move(candidates), source_location_from(node));
    ctx.mark_defined(target);
}
```

这样：

- `A(end)` 会得到一条候选
- `A(floor(end))` 会得到两条候选，顺序是内层在前、外层在后
- `A(1 + end, foo(end))` 会分别记录正确的 `index_position`

也就是说，`MagicEndNode` 记录的是：

- “我可能绑定到哪些 apply 输入位点”

而不是：

- “我已经绑定到了哪一个 base”

### 3. untyped SSA

SSA 层保留完全相同的语义切分：

- `SSAApplyNode`
- `SSACallNode`
- `SSAMagicEndNode`

而不是重新把它们压回一个统一的 `SSACallNode`。

建议形状：

```cpp
class SSAApplyNode final : public UntypedSSANode {
public:
    struct Head {
        enum Type {
            DirectName,
            IndirectValue,
        };

        Type type = DirectName;
        std::string direct_name;
        ValueRef indirect_value;
    };

    SSAApplyNode(ApplySiteId site_id, Head head, std::vector<ValueId> results,
                 std::vector<ValueRef> inputs,
                 std::optional<SourceLocation> location = std::nullopt);

    ApplySiteId site_id() const;
    const Head& head() const;
    const std::vector<ValueId>& results() const;
    const std::vector<ValueRef>& inputs() const;
};

class SSACallNode final : public UntypedSSANode {
public:
    struct DirectCallee {
        enum Kind {
            Builtin,
            Internal,
            MFunction,
        };

        Kind kind = Builtin;
        std::string symbol;
    };

    struct Callee {
        enum Type {
            Direct,
            Indirect,
        };

        Type type = Direct;
        DirectCallee direct;
        ValueRef indirect_value;
    };

    SSACallNode(Callee callee, std::vector<ValueId> results, std::vector<ValueRef> inputs,
                std::optional<SourceLocation> location = std::nullopt);

    const Callee& callee() const;
    const std::vector<ValueId>& results() const;
    const std::vector<ValueRef>& inputs() const;
};

class SSAMagicEndNode final : public UntypedSSANode {
public:
    SSAMagicEndNode(ValueId result, std::vector<EndBindingCandidate> candidates,
                    std::optional<SourceLocation> location = std::nullopt);

    ValueId result() const;
    const std::vector<EndBindingCandidate>& candidates() const;
};
```

对应关系应是：

- `ApplyNode -> SSAApplyNode`
- `CallNode -> SSACallNode`
- `MagicEndNode -> SSAMagicEndNode`

这样 `NonSSA -> UntypedSSA` 只是值表示方式变化，不会再次丢掉 `A(...)` 的歧义信息。

`ConstructUntypedSSA` 对这三类节点的处理应是纯结构映射：

- `ApplyNode::Head::DirectName -> SSAApplyNode::Head::DirectName`
- `ApplyNode::Head::IndirectValue -> SSAApplyNode::Head::IndirectValue(ValueRef{...})`
- `MagicEndNode::candidates -> SSAMagicEndNode::candidates`
- `CallNode::Direct/Indirect -> SSACallNode::Direct/Indirect`

也就是说：

- 过 SSA 时不做 `Apply -> Call` 的语义收敛
- 这个收敛如果要做，应交给单独的 pass 或解释器

### 4. 运行时分派

解释器需要新增一份显式的 apply/index 执行态，例如：

```cpp
struct ActiveIndexContext {
    ApplySiteId site_id;
    Value::Object base;
};

struct ExecutionState {
    ...
    std::vector<ActiveIndexContext> active_index_contexts;
};
```

这里 `active_index_contexts` 里保存的是：

- 当前哪些 `ApplySiteId` 已经被判成“索引取值”
- 对应的 base 对象是什么

解释器对 `SSAApplyNode` 的执行逻辑应是：

1. 先解析 head，但此时还不计算输入参数
2. 决定这层 apply 的模式
3. 再按模式去求值输入并执行

更具体地说：

```cpp
ResolvedApply resolve_apply_head(const SSAApplyNode& node, ExecutionState& state) {
    if (node.head().type == DirectName) {
        if (name resolves to builtin)   return FunctionDirect(Builtin, symbol);
        if (name resolves to internal)  return FunctionDirect(Internal, symbol);
        if (name resolves to m-function)return FunctionDirect(MFunction, symbol);
        throw ...; // 当前模型下，DirectName 不应回退成 block get
    }

    Value::Object head_value = state.require_concrete_object(node.head().indirect_value, "apply head");
    if (head_value->type() == ba_function_handle) {
        return FunctionIndirect(head_value);
    }
    return BlockGet(head_value);
}
```

然后执行顺序是：

```cpp
std::vector<Value> evaluate_apply(const SSAApplyNode& node, ExecutionState& state) {
    ResolvedApply resolved = resolve_apply_head(node, state);

    if (resolved.kind == BlockGet) {
        state.active_index_contexts.push_back({node.site_id(), resolved.base});
        auto guard = finally([&] { state.active_index_contexts.pop_back(); });

        std::vector<Value::Object> args = evaluate_apply_inputs(node.inputs(), state);
        args = flatten_call_inputs(args);
        return invoke_runtime_paren_get(resolved.base, args, node.results().size());
    }

    std::vector<Value::Object> args = evaluate_apply_inputs(node.inputs(), state);
    args = flatten_call_inputs(args);
    return invoke_resolved_function_apply(resolved, args, node.results().size(), state);
}
```

这里最关键的一点是：

- 对 `BlockGet`，必须在求值输入参数之前先 push `site_id`
- 对 `FunctionInvoke`，不能 push `site_id`

这样 `A(floor(end))` 才会得到正确行为：

1. 若外层 `A(...)` 先被判成索引，则外层 `site_id` 先入栈
2. 再去求值参数 `floor(end)`
3. 若 `floor(...)` 是函数调用，则内层 `site_id` 不入栈，`end` 绑定外层
4. 若 `floor(...)` 是索引，则内层 `site_id` 入栈，`end` 优先绑定内层

`SSAMagicEndNode` 的执行则应是：

```cpp
Value evaluate_magic_end(const SSAMagicEndNode& node, ExecutionState& state) {
    for (const EndBindingCandidate& candidate : node.candidates()) {
        auto it = find_active_index_context(candidate.site_id, state.active_index_contexts);
        if (it == state.active_index_contexts.end()) {
            continue;
        }

        return invoke_magic_end_builtin(it->base, candidate.index_position,
                                        candidate.total_index_count);
    }

    throw ...; // `end` 没有绑定到有效的索引上下文
}
```

也就是说，`SSAMagicEndNode` 的求值本身不需要保存 base：

- base 来自当前激活的索引上下文
- `candidate` 只负责提供 `index_position` / `total_index_count`

这里还可以有一个可选的中间 pass：

- 当某个 `SSAApplyNode` 已被静态证明一定是函数调用时
- 可以把它提前收敛成 `SSACallNode`

例如：

- direct name 已确定解析到 builtin / internal / module function
- 或 indirect head 已确定是函数句柄值

这样解释器仍然只需要处理两类稳定节点：

- 仍然保留歧义的 `SSAApplyNode`
- 已经收敛完成的 `SSACallNode`

### 5. 对优化和分析的好处

把 `Apply` 和 `Call` 拆开之后，收益很直接：

- `SSACallNode` 的 direct callee 拥有明确类别
- 常量折叠可以只针对 `Builtin` 白名单
- effect / purity 建模可以分别处理 `Builtin`、`Internal`、`MFunction`
- 过程间分析可以直接识别 `MFunction`
- `magic_end` 不再依赖“误把 `A(...)` 编成 call”这一层临时编码

因此，从长期 IR 设计上看，真正应重设计的是 call 这一族节点，而不是只给 `magic_end` 打补丁。

## 后续设计边界

后续路线仍然应明确分成三层：

1. `NonSSA`
2. `UntypedSSA`
3. `TypedSSA`

其中当前第 1 层和第 2 层已经落地，第 3 层仍待设计。

后续如果进入 `TypedSSA`，应继续保持：

- 容器层尽量复用
- 节点语义单独定义
- verifier / printer / interpreter / optimizer 按 stage 明确分层
