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

第一版建议使用：

```cpp
using Value = std::shared_ptr<ba_obj>;

struct Binding {
    Value value;
    bool initialized = false;
};

class Frame {
public:
    using SymbolTable = std::unordered_map<std::string, Binding>;

    Function* function = nullptr;
    Frame* caller = nullptr;
    int nargin = 0;
    int nargout = 0;
    bool returned = false;
    SymbolTable symbols;
    std::vector<Value> outputs;
};
```

原因是当前 IR 中：

- `NameInstruction` 通过名字引用变量
- `AssignInstruction` 通过名字写变量
- `Function` 会持有输入输出参数名列表

因此第一版按名字管理运行时变量最省事，也最贴近 MATLAB-like 工作区语义。

建议 `Frame` 提供最小接口：

- `declare(name)`
- `store(name, value)`
- `load(name)`
- `contains(name)`
- `is_initialized(name)`

其中：

- `load(name)` 对“名字不存在”或“已声明但未初始化”都应报错
- 输入参数在建帧时按 `Function::input_names()` 写入 `symbols`
- 输出参数在建帧时按 `Function::output_names()` 预声明
- `ReturnInstruction` 只负责结束执行，函数返回值由 `Function::output_names()` 从 `symbols` 中顺序收集

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
  - 从 `frame.symbols` 读取变量值
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
  - 写入 `frame.symbols[name]`
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

- `CondJumpInstruction`
- `JumpInstruction`
- `ReturnInstruction`

语义建议如下：

- `CondJumpInstruction`
  - 计算条件表达式
  - 根据结果跳转到 `true_block` 或 `false_block`
- `JumpInstruction`
  - 直接返回目标 block
- `ReturnInstruction`
  - 结束函数执行
  - 返回到调用方

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

`CondJumpInstruction` 的条件最终会落到一个 `std::shared_ptr<ba_obj>` 上，因此还需要统一条件判断入口：

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

## 当前主线

在 `simple_demo.m` 跑通以后，当前阶段的主线不再是重新设计执行架构，
而是持续扩大：

- `AST -> IR lowering` 的语法覆盖面
- `IR -> runtime` 的解释执行语义覆盖面

也就是说，接下来的重点是让更多 MATLAB-like 语法能够：

1. 被稳定 lower 成 IR
2. 被当前 IR 解释器正确执行

## 接下来的重点

### 1. 扩大 lowering 覆盖

优先支持投入产出比较高的 AST 节点，例如：

- `return`
- 单目运算
- 更多比较/逻辑运算
- `elseif`
- `while`
- `for`
- 多返回值函数调用
- 普通 `m` 函数定义和调用
- 局部函数
- `break/continue`

### 2. 补齐解释器语义

随着 lowering 覆盖面扩大，解释器侧也需要同步补齐：

- 函数输入输出参数绑定
- `nargin/nargout`
- `ans`
- 更清晰的未定义变量/未初始化变量报错
- builtin 调用错误透传
- script workspace 和 function workspace 的差异
- 后续的 `persistent/global`

## 当前阶段的判断

因此，当前最重要的工作可以概括为两句话：

1. 不急着再换执行架构，而是先把现有 `IR + Interpreter` 主链做厚。
2. 让更多 `m` 语法能够被 lower 并执行，是这段时间最核心的演进方向。

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
