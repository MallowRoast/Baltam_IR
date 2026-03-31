# Baltam_IR 执行方案整理

## 当前状态

当前项目已经不再是“只有 parser 和 AST 打印”的阶段，而是具备了完整的主执行链：

`parse -> lowering -> print_ir -> execute_function`

也就是说，项目内已经有：

- `AST -> IR lowering`
- IR 文本打印
- IR 解释执行

因此这份文档不再讨论“是否要开始做 IR 执行链”，而是说明当前执行架构应如何继续演进。

## 当前主线

对 Baltam_IR，更合适的主线仍然是：

`AST -> IR Lowering -> Verify/Optimize(optional) -> IR 解释执行 -> Profile -> 热点 JIT`

而不是：

- 回到递归 AST 解释作为主路径
- 在基础设施还不完整时直接改成全量 LLVM/MLIR JIT

## 当前执行架构的分工

### 1. 前端层

职责：

- 调用 parser 得到 AST
- 保留源码级结构和位置信息

### 2. Lowering 层

职责：

- 把 AST 转成显式 CFG IR
- 为指令挂接稳定的 `ValueId`
- 在结构化控制流处生成当前版本的 `phi`

### 3. IR 层

职责：

- 承载 `Module` / `Function` / `BasicBlock`
- 承载值结果模型
- 作为打印、执行、分析和后续优化的统一载体

### 4. 解释器层

职责：

- 执行当前 IR
- 作为现阶段主执行路径
- 为后续 profile、优化和 JIT 保留统一入口

## AST evaluator 的定位

`baltam_kernel` 中已有 AST evaluator 仍然有价值，但它更适合作为：

- 语义参考实现
- 回退语义来源
- 调试和对照工具

它不应继续作为 Baltam_IR 自身的长期主执行架构。

## 为什么当前路线仍然合理

当前自研 IR 已经具备这些长期价值：

- 显式 `BasicBlock`
- 显式 CFG
- 显式 terminator
- `ValueId` / `ValueRef`
- 可执行的 `PhiInstruction`
- 多结果调用
- 源码位置信息

这使它天然适合作为：

- 解释执行载体
- 优化器输入
- 未来热点 JIT 的前端 IR

## 当前最该补的不是 JIT，而是基础设施

如果从执行链角度看，接下来最重要的工作顺序应是：

1. 扩大 lowering 覆盖面
2. 扩大解释器语义覆盖面
3. 引入 `Verifier`
4. 引入 `PassManager`
5. 引入 analysis / SSA / 基础优化
6. 之后再做 profile 和热点 JIT

也就是说，JIT 不是下一步，而是更后面的事情。

## 建议的阶段顺序

### Phase 1：稳住主执行链

目标：

- 让更多 `m` 语法能够 stable lower
- 让解释器能稳定执行这些 IR
- 增加测试覆盖和错误信息质量

### Phase 2：把执行链接到优化链

目标：

- 在 lowering 之后加入 verifier
- 新增 `optimize_module()` / `optimize_function()`
- 为分析与优化 pass 建稳定入口

### Phase 3：建立 analysis 和 SSA

目标：

- CFG analysis
- dominator
- dominance frontier
- def-use
- `BuildPrunedSSA`

### Phase 4：再做 profile

目标：

- 热点计数
- 基本类型/shape 反馈
- 为未来热点专门化提供依据

### Phase 5：最后再做 JIT

目标：

- 只编译热点、稳定、低风险片段
- 失败时可回退到解释器

## 当前不建议做的事

为了控制风险，当前不建议：

- 直接删掉 AST evaluator 相关依赖
- 先做全量 JIT 再补 verifier/analysis
- 在执行链还没稳定时大改 IR 结构
- 把解释器和 SSA 改造绑成一次性重写

## 总结

当前 Baltam_IR 的执行架构已经从“设计中”进入“可运行主链”阶段。

因此后续工作的重点应转为：

- 补执行质量
- 补 verifier 和优化器入口
- 在现有 IR 主链之上逐步引入 SSA、profile 和热点 JIT
