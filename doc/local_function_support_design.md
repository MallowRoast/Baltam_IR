# Local 函数支持方案

本文记录当前 `IR` 体系下，对 `local` 函数支持的最小设计方案。目标不是一次性覆盖全部 Matlab 函数解析规则，而是先支持当前测试样例里的这两条语义：

1. `test0_2` 脚本中对 `sin` 的调用仍然保留为 `apply`
2. `test0_3` 函数中对 `sin` 的调用可以直接分派到当前文件内的 `local` 函数

## 1. 当前目标与边界

当前只支持文件内 `local` 函数，不处理：

- `nested function`
- closure 捕获
- `private`
- `import`
- path 上的同名函数竞争
- 完整的函数优先级系统

当前需要解决的是：

- 如何在 `MFileUnit` 中知道哪些 `FunctionUnit` 是 local 函数
- 如何在 `CallInst` 中明确表达“这个调用已经静态分派到本文件内某个 local 函数”

## 2. 样例语义

### 2.1 `test0_2`

`test0_2` 是脚本文件，末尾有一个 `local` 函数 `sin(x) = x + 1`。

虽然 parser 已经能告诉我们文件里存在这个 local 函数，但当前脚本主体中的：

```matlab
b = sin(a);
```

仍然应该保留为 `apply`，因为当前设计里，脚本名字语义仍然依赖运行时 workspace。

也就是说，脚本里的 local 函数定义目前只做“文件结构记录”，不触发 `apply -> call` 收敛。

### 2.2 `test0_3`

`test0_3` 是函数文件，文件内同时存在主函数 `test0_3` 和 local 函数 `sin`。

在主函数中：

```matlab
b = sin(a);
```

可以直接收敛为 `call`，因为：

- 当前处于 function unit，而不是 script unit
- `sin` 没有被绑定成局部变量
- 当前文件内已经静态知道存在 local 函数 `sin`
- 在当前实现范围内，local 函数的优先级最高

## 3. 文件级函数识别

### 3.1 parser 行为

当前 parser 对带 local 函数的文件，会返回多个 `pcdata`。

从现有样例看：

- `test0_2.m` 会返回两个 `pcdata`
  - 一个是脚本主体
  - 一个是 local 函数 `sin`
- `test0_3.m` 也会返回两个 `pcdata`
  - 一个是主函数 `test0_3`
  - 一个是 local 函数 `sin`

当前实现直接依赖 parser 给出的顺序约定：

- 脚本单元或“函数名等于文件名”的单元视为主单元
- 其余函数单元视为 local 函数

### 3.2 入口单元选择

因此 lowering 当前不再额外推断入口单元，而是直接采用：

- 脚本单元或函数名与文件名一致的单元对应入口单元
- 其余 `FunctionUnit` 都视为 local 函数

例如：

- `test0_2.m` 中脚本单元是入口单元
- `test0_3.m` 中 `test0_3` 与文件名一致，因此它是主函数
- 同文件内的 `sin` 视为 local 函数

## 4. `MFileUnit` 中记录 local 函数

建议在 `MFileUnit` 上增加两份文件级索引：

```cpp
std::unordered_map<InternedString, FunctionUnit*> local_function_map;
```

当前最小实现里，只保留 `local_function_map` 就够了。它的作用是：

- 便于 lowering 期按名字快速查找
- 让 local 函数信息不只是 lowering 临时状态，而是成为文件级 IR 的正式结构信息

## 5. `CallInst` 如何表达 local 分派结果

当前 `CallInst` 只有：

- `Direct`
- `Indirect`

这不足以区分：

- “按名字直接调用某个 symbol”
- “已经静态命中当前文件内某个 local 函数”

因此建议把 `CallInst::CalleeKind` 扩成：

```cpp
enum CalleeKind : std::uint8_t {
    Direct,
    Local,
    Indirect,
};
```

同时在 `CallInst` 中增加：

```cpp
FunctionUnit* local_target = nullptr;
```

其中：

- `Direct`：仍沿用 `callee = InternedString(...)`
- `Local`：`callee` 仍可保留函数名，真正的静态目标由 `local_target` 指向
- `Indirect`：继续通过 `ValueId` / `SlotId` 等 operand 表达

这样可以明确表达：

- 这个调用不是普通 symbol call
- 它已经静态分派到当前文件中的某个具体函数单元

## 6. lowering 分派规则

当前 function 中的名字调用大致规则是：

1. 若名字已绑定成变量，则保留 `apply`
2. 否则直接收敛成 `call`

加入 local 函数支持后，建议改成：

1. 若当前 unit 是 `script`，仍保留 `apply`
2. 若当前 unit 是 `function` 且名字已绑定成变量，保留 `apply`
3. 若当前 unit 是 `function` 且命中文件内 local 函数，生成 `CallInst(kind=Local)`
4. 否则生成普通 `CallInst(kind=Direct)`

这正好覆盖了当前的两条样例语义：

- `test0_2`：脚本，所以仍然是 `apply`
- `test0_3`：函数，且命中 local `sin`，所以收敛成 `Local call`

## 7. builder 状态管理

为了让主函数在 lowering 时就能拿到 local 函数对应的 `FunctionUnit*`，需要先创建完整文件内的所有 `CodeUnit`，再开始 lowering 各自的主体。

因此 lowering 需要分成两个阶段：

1. 预创建全部 `ScriptUnit` / `FunctionUnit`
2. 回到各自 unit 中 lower 主体 AST

这要求 `IRBuilder` 不再只维护“当前唯一一个 unit 状态”，而是能：

- 为每个 `CodeUnit` 保存一份 `IRUnitBuildState`
- 在 lowering 不同 unit 时切换当前活动 unit

## 8. 打印建议

为了让文本 IR 能看出 local 分派已经发生，建议 `printer` 对 local call 单独打印。

例如：

```text
%3 = call @sin(%4)
```

可以改成：

```text
%3 = call_local @sin(%4)
```

或其他等价格式。关键是要能区分：

- 普通 direct symbol call
- 已经命中的 local function call

## 9. 最小实现顺序

建议按以下顺序落地：

1. 在 `MFileUnit` 中记录 local 函数索引
2. 调整 `IRBuilder`，支持预创建全部 unit 后再切换回来 lowering
3. 修改 `IRLowerer`，按“脚本或函数名等于文件名”的规则识别主单元和 local 函数
4. 扩展 `CallInst`，支持 `Local` callee kind
5. 更新 `IRPrinter`
6. 为 `test0_2` / `test0_3` 增加 smoke test

## 10. 当前阶段的取舍

当前方案刻意保持保守：

- 脚本里的 local 函数暂时只记录，不参与 `apply -> call` 收敛
- 函数里的 local 函数只在“未被变量遮蔽”时静态命中
- 不提前处理 nested / closure / import / private 等更复杂名字语义

这样可以先把文件内 local 函数支持打通，再逐步扩展到更完整的函数解析系统。
