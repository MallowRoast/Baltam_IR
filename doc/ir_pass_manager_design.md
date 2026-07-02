# IR PassManager 设计

## 目标

`IRPassManager` 是 IR lowering 之后、bytecode/frame lowering 之前的 pass 调度入口。第一版
重点解决三件事：

- 统一 module / file / code unit 三个作用域的 pass 接口。
- 统一 pass 诊断、是否修改 IR、运行统计。
- 为 pass 前后接入 verifier 预留稳定钩子。

它不直接实现任何优化，也不在第一版缓存 analysis 结果。具体 pass 只需要声明自己的作用域，
并在对应 IR 对象上返回 `IRPassResult`。

## 非目标

- 不把 pass 逻辑塞回 `IRLowerer`。lowering 仍负责生成 canonical high-level IR。
- 不在第一版引入复杂的 analysis manager / invalidation graph。
- 不强制所有工具默认运行优化 pass。`ir_print` 默认仍应打印 lowering 原始形状。
- 不让具体 pass 直接决定整条 pipeline 是否继续；它只通过 error 诊断表达失败，由
  `IRPassManagerOptions::stop_on_error` 控制是否短路。

## 作用域

第一版提供三个基类：

```cpp
class IRModulePass;
class IRFilePass;
class IRCodeUnitPass;
```

调度顺序固定为：

1. `IRModulePass`：对整个 `IRModule` 运行一次。
2. `IRFilePass`：按 `IRModule::files` 顺序对每个 `MFileUnit` 运行一次。
3. `IRCodeUnitPass`：先按文件顺序访问每个文件的 `code_units`，再按
   `IRModule::anonymous_functions` 表顺序访问匿名函数体。

`IRPassContext` 会带上当前可见的 `module`、`file` 和 `unit` 指针。匿名函数体的 `file`
通过 `lexical_parent` 反查；如果上下文不完整，则允许为 `nullptr`，具体 pass 需要按自己的
语义处理。

## 结果模型

每个 pass 返回：

```cpp
struct IRPassResult {
    bool changed;
    std::vector<IRPassDiagnostic> diagnostics;
};
```

`changed` 只表示高层 IR 或 pass 产物发生了变化。第一版没有 analysis cache，因此无需更细的
invalidation 标记。后续如果引入 def-use、dominator tree、slot liveness 等 analysis，可以在
这个结果模型上扩展 preserved / invalidated 集合。

`IRPassManagerResult` 汇总：

- `changed`：任一 pass invocation 修改了 IR 时为 true。
- `diagnostics`：所有 pass 和 verifier 诊断。
- `pass_runs`：每个 pass 的作用域、调用次数、修改次数。

## Verifier 钩子

`IRPassManagerOptions` 提供三个开关：

- `verify_before_pipeline`
- `verify_after_each_pass`
- `verify_after_pipeline`

这些开关默认关闭，避免把 verifier 成本强加给 release pipeline。开发和测试场景可以打开
`verify_after_each_pass`，定位是哪一个 pass 第一次破坏 IR 约束。

## 推荐 pipeline

当前阶段建议先保持保守：

```text
AST
  -> IRLowerer
  -> verify_ir
  -> optional cleanup / specialization passes
  -> InternalLocalReusePass
  -> frame layout / bytecode lowering
```

短期内可以先接入不改写 IR 的 pass，例如 `InternalLocalReusePass` 的物理 slot 分配表输出。
等 slot remap、def-use 和 value replacement 基础设施稳定后，再接入会改写 IR 的 cleanup /
constant folding pass。
