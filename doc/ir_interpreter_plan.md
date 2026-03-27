# Baltam_IR 解释执行思路

## 目标

本文档整理当前这版 IR 的直接执行方案。前提如下：

- 运行时值统一表示为 `std::shared_ptr<ba_obj>`
- 运算符如 `+`、`*`、`>` 已有对应实现
- 内建函数如 `sin` 已有对应实现

目标不是立即设计高性能执行器，而是先明确一版最小、可落地的 IR 解释器。

## 核心判断

在当前条件下，IR 解释器应该做成一个很薄的执行层：

- 不重新发明值系统
- 不重新实现内建函数
- 不重新实现运算符
- 只负责调度 IR 节点，并把执行请求转发到现有 runtime

## 需要准备的内容

### 1. 执行帧 Frame

每次执行一个 `Function`，都需要一个运行时执行帧。

第一版最简单的形式可以是：

- `std::unordered_map<std::string, std::shared_ptr<ba_obj>> locals`

原因是当前 IR 中：

- `NameInstruction` 通过名字引用变量
- `AssignInstruction` 通过名字写变量

所以第一版直接按名字管理变量最省事。

后续如果要优化，可以再逐步替换成 slot 表。

### 2. 表达式求值入口

建议准备一个统一入口：

```cpp
std::shared_ptr<ba_obj> eval_expr(Instruction* inst, Frame& frame);
```

第一版主要处理：

- `NameInstruction`
- `NumberInstruction`
- `BinOpInstruction`
- `CallInstruction`

语义建议如下：

- `NameInstruction`
  - 从 `frame.locals` 读取变量值
- `NumberInstruction`
  - 将字面量包装成对应的 `ba_obj`
- `BinOpInstruction`
  - 递归求左右值
  - 调用已有的运算符实现
- `CallInstruction`
  - 递归求输入参数
  - 调用已有的函数实现

### 3. 语句执行入口

建议准备一个语句执行入口：

```cpp
void exec_inst(Instruction* inst, Frame& frame);
```

第一版主要处理：

- `AssignInstruction`
- `CallInstruction`

语义建议如下：

- `AssignInstruction`
  - 计算右值
  - 写入 `frame.locals[name]`
- `CallInstruction`
  - 计算输入参数
  - 调用 runtime 中已有的函数实现
  - 将结果写入 `out_args`

## 控制流执行

### 4. Block 调度循环

每个 `Function` 的执行，从 `entry_block()` 开始。

建议执行流程：

1. 顺序执行 `block->instructions()`
2. 执行 `block->terminal()`
3. 根据 terminal 的结果跳到下一个 block

可表达为：

```cpp
BasicBlock* bb = func.entry_block();
while (bb != nullptr) {
    for (Instruction* inst : bb->instructions()) {
        exec_inst(inst, frame);
    }
    bb = exec_terminal(bb->terminal(), frame);
}
```

### 5. Terminal 执行入口

建议准备一个 terminal 调度入口：

```cpp
BasicBlock* exec_terminal(Instruction* term, Frame& frame);
```

第一版处理：

- `IfInstruction`
- `JumpInstruction`
- `ReturnInstruction`

语义建议如下：

- `IfInstruction`
  - 计算条件表达式
  - 根据结果跳转到 `true_block` 或 `false_block`
- `JumpInstruction`
  - 直接返回目标 block
- `ReturnInstruction`
  - 结束函数执行
  - 返回函数结果或记录返回值

## 运行时桥接层

因为数值系统、运算符和函数实现已经存在，IR 解释器最重要的是桥接。

建议把桥接集中到两类接口：

### 运算符桥接

```cpp
std::shared_ptr<ba_obj> eval_binop(BinOpInstruction::Type op,
                                   std::shared_ptr<ba_obj> lhs,
                                   std::shared_ptr<ba_obj> rhs);
```

职责：

- 按 `BinOpInstruction::Type` 分派到已有的 `+`、`*`、`>` 实现

### 函数调用桥接

```cpp
std::vector<std::shared_ptr<ba_obj>> eval_call(const std::string& name,
                                               const std::vector<std::shared_ptr<ba_obj>>& in_args);
```

职责：

- 按函数名调用已有 runtime/builtin 层实现
- 返回输出参数列表

这样做的好处是：

- IR 层只关心调度，不关心具体 runtime 细节
- 解释器结构更薄
- 后续替换调用策略更容易

## 条件判断

### 6. 条件值转换

`IfInstruction` 的条件最终会落到一个 `std::shared_ptr<ba_obj>` 上，因此还需要统一条件判断入口：

```cpp
bool to_cond(const std::shared_ptr<ba_obj>& value);
```

这个函数应负责复用 MATLAB-like 语义，例如：

- logical scalar
- 非零数值
- 可能的矩阵条件行为

它是控制流执行中的关键桥接点。

## 建议的最小实现顺序

建议按下面顺序推进：

1. 定义 `Frame`
2. 实现 `eval_expr`
3. 实现 `exec_inst`
4. 实现 `exec_terminal`
5. 建立 `eval_binop`
6. 建立 `eval_call`
7. 用 `simple_demo.m` 跑通整条链路

## 为什么这版解释器值得做

当前这版 IR 解释器虽然还不会比 AST 解释器高级很多，但它有一个重要优势：

- 执行对象已经从“树”变成了“显式 CFG + 指令序列”

这为后续工作创造了基础条件：

- profile
- 热点识别
- block 级优化
- JIT
- deopt / fallback

因此，这版解释器的意义主要不是“立刻比 AST 解释器更强”，而是“为后续执行体系提供正确的载体”。

## 总结

在现有 runtime 条件下，IR 解释器的最小方案可以概括为：

- 一个 `Frame`
- 一组表达式求值函数
- 一组语句执行函数
- 一个 block 调度循环
- 一个 terminal 跳转入口
- 两类运行时桥接：运算符和函数

而运行时值继续统一复用：

- `std::shared_ptr<ba_obj>`

这是当前最直接、风险最低、最容易从 demo 走向可执行原型的路径。

