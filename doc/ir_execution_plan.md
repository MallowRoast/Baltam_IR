# Baltam_IR 执行方案整理

## 背景

当前项目入口位于 `src/main.cpp`，主要完成：

- 初始化 `bt_ast_interface`
- 解析 `.m` 文件
- 打印 AST

项目自身还没有形成一条完整的执行链路。与此同时，`baltam_kernel` 已经提供了较完整的 AST 递归求值能力、符号查找和脚本执行能力。因此，当前最务实的方向不是直接实现一套 Julia 风格的全量 JIT，而是先建立一层适合 MATLAB-like 语言的 IR 执行框架。

## 总体判断

对于 MATLAB-like 语言，更合适的路线是：

`AST -> IR Lowering -> IR 解释执行 -> Profile 收集 -> 热点 JIT`

而不是：

`AST -> 全量 LLVM JIT -> 直接替代所有执行路径`

原因主要有三点：

1. MATLAB-like 语言的动态性强，变量类型、shape、调用目标经常要到运行时才能稳定。
2. 数组语义、内置函数、路径查找、函数句柄、`eval` 等特性不利于全量静态专门化。
3. 现有 `baltam_kernel` 已有 AST evaluator，可作为语义参考和回退路径，没有必要一开始彻底推翻。

## 推荐架构

建议把执行系统拆成四层。

### 1. 前端层

职责：

- 调用 `bt_ast_interface` 或现有 parser 得到 AST
- 保留原始语法信息，供报错、调试、源码映射使用

输入：

- `.m` 文件或字符串

输出：

- AST

### 2. IR Lowering 层

职责：

- 将 Baltam AST 降成更稳定、更容易执行和优化的中间表示
- 显式化控制流
- 为后续 profile、优化、JIT 提供统一入口

建议先定义一个较小的 HIR/Bytecode 风格 IR，覆盖：

- `const`
- `load_local`
- `store_local`
- `binary_op`
- `unary_op`
- `cmp_op`
- `call_builtin`
- `branch`
- `jump`
- `return`

对于 `if/while/for`，建议都 lower 成 basic block + 显式跳转，而不是保留树形递归结构。

### 3. IR 解释器层

职责：

- 作为新的主执行路径
- 执行 lowered IR，而不是直接递归遍历 AST
- 收集运行时类型信息和热点信息

运行时至少记录：

- slot 的值类别：`double scalar`、`logical scalar`、`matrix`、`complex`、`object`
- shape 信息：是否标量、行列规模是否稳定
- 调用分派信息：内置函数、用户函数、动态调用
- 热点计数：函数入口计数、循环回边计数、调用点计数

### 4. 热点 JIT 层

职责：

- 只编译高收益、低风险的热点片段
- 通过 guard 保证假设成立
- 失败时回退到 IR 解释器

第一阶段建议只覆盖：

- 标量算术
- 标量比较
- 简单条件分支
- 简单循环
- 少量稳定 builtin，如 `sin/cos/exp`

不建议第一阶段 JIT 的内容：

- `eval`
- 动态字段访问
- 不稳定的函数句柄调用
- `classdef`
- 复杂 `cell/struct`
- 高度动态的名称解析

## 递归 AST 解释器是否需要抛弃

结论：**不需要立刻抛弃，但不建议继续把它作为长期主执行架构。**

更合理的定位是：

- 短期：保留 AST 解释器，作为语义参考实现
- 中期：IR 解释器成为主执行路径
- 长期：AST 解释器只保留为调试、验证、回退或不支持语义的兜底后端

也就是说，不是“删掉 AST 解释器再做 IR”，而是“逐步把主路径迁移到 IR，把 AST 解释器退居二线”。

## 为什么不能长期停留在递归 AST 解释器

递归 AST 解释器的主要问题有：

1. 控制流隐含在树结构里，不利于分析和优化。
2. 很难系统性插入 profile 点。
3. 很难对局部热点做专门化编译。
4. AST 节点往往带有语法噪声，不适合作为长期执行载体。
5. 后续做 SSA、值编号、常量传播、类型特化时成本高。

所以即使短期继续使用 AST evaluator，也应把它看作过渡方案，而不是最终方案。

## 为什么不应该马上删掉 AST 解释器

如果现在就完全放弃 AST evaluator，会有几个现实风险：

1. 新 IR 的语义边界一开始不完整，容易与现有行为偏离。
2. 出现复杂语义时，缺少稳定的参考后端，不利于排错。
3. 开发节奏会被一次性“大迁移”拖慢。

保留 AST evaluator 的价值在于：

- 对照验证 IR 执行结果
- 为暂不支持的语法做 fallback
- 帮助确认 lowering 是否保持语义一致

## 推荐迁移策略

建议按下面的顺序推进。

### 阶段 0：维持现状，补足观测能力

目标：

- 保留当前 parse + AST 打印能力
- 增加最小的执行实验入口
- 明确 AST 节点种类和脚本结构

产出：

- AST dump
- 节点统计
- 简单脚本覆盖样例

### 阶段 1：建立最小 IR

目标：

- 支持 `simple_demo.m` 这类脚本的 lowering
- 先只覆盖：
  - 常量
  - 局部变量
  - 二元运算
  - 内置函数调用
  - `if`
  - `return`

产出：

- `AST -> IR` lowering
- IR 文本打印器

### 阶段 2：实现 IR 解释器

目标：

- 不再直接执行 AST
- 让 `simple_demo.m` 可以通过 IR 跑通

产出：

- frame / slot 模型
- basic block 调度
- builtin 调用桥接

### 阶段 3：引入 profile

目标：

- 为每个函数、循环和调用点收集热点信息
- 为每个 slot 收集粗粒度类型信息

产出：

- hot counter
- type feedback
- shape feedback

### 阶段 4：实现热点 JIT

目标：

- 只编译热循环和热函数
- 带 guard 执行
- 失败时 deopt 回 IR 解释器

产出：

- 热点识别
- 专门化版本缓存
- deopt/fallback 机制

## 对当前项目的直接建议

如果现在开始动手，建议优先做这些模块：

- `src/ir/`
  - IR 节点定义
  - basic block 定义
  - function/module 定义
- `src/lowering/`
  - `AST -> IR` lowering
- `src/interpreter/`
  - IR 解释器
- `src/runtime/`
  - slot value
  - type tag
  - builtin bridge
- `src/debug/`
  - IR dump
  - profile dump

## 一个简单 lowering 示例

以：

```matlab
a = 1 + 2;
b = sin(a);

if b > 0
    c = b * 2;
else
    c = 0;
end
```

为例，建议 lower 成类似：

```text
block0:
  r0 = const 1
  r1 = const 2
  r2 = add r0, r1
  store a, r2
  r3 = load a
  r4 = call_builtin sin, r3
  store b, r4
  r5 = load b
  r6 = const 0
  r7 = cmp_gt r5, r6
  branch r7, block1, block2

block1:
  r8 = load b
  r9 = const 2
  r10 = mul r8, r9
  store c, r10
  jump block3

block2:
  r11 = const 0
  store c, r11
  jump block3

block3:
  return
```

这个形式已经足够支撑：

- 解释执行
- CFG 分析
- profile 插桩
- 热点块编译

## 最终建议

结论可以压缩成两句话：

1. **现有递归 AST 解释器不该马上抛弃，但应该停止把它当作最终执行架构。**
2. **接下来的主线工作应转向基于 IR 的执行器，并让 AST evaluator 逐步退化成参考实现和 fallback。**

这条路线兼顾了：

- 现有代码复用
- 语义安全性
- 后续优化空间
- 实际工程推进速度

