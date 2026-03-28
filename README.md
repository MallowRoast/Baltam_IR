# Baltam_IR

`Baltam_IR` 目前处于 IR 设计和原型阶段。

当前仓库已经具备：

- 读取并打印 `test/simple_demo.m` 对应的最小 IR
- 基础 IR 对象模型：
  - `Module`
  - `Function`
  - `BasicBlock`
  - `Instruction`
- 一组最小指令节点：
  - `NameInstruction`
  - `NumberInstruction`
  - `BinOpInstruction`
  - `AssignInstruction`
  - `CallInstruction`
  - `IfInstruction`
  - `JumpInstruction`
  - `ReturnInstruction`

## 下一步：实现 IR 解释器

下一步建议直接实现一版最小 IR 解释器，并复用现有运行时能力。

### 目标

目标不是重新设计运行时，而是让当前 IR 可以直接执行。

执行时应尽量复用已有基础设施：

- 运行时值统一使用 `std::shared_ptr<ba_obj>`
- 运算符如 `+`、`*`、`>` 复用已有实现
- 内建函数如 `sin` 复用已有实现

### 建议的最小组件

#### 1. Frame

每次执行一个 `Function`，创建一个执行帧。

第一版可以直接使用名字表：

```cpp
std::unordered_map<std::string, std::shared_ptr<ba_obj>> locals;
```

理由：

- `NameInstruction` 通过变量名读取值
- `AssignInstruction` 通过变量名写值
- `CallInstruction` 的 `out_args` 也可以先通过名字绑定结果

#### 2. 表达式求值

准备一个统一入口：

```cpp
std::shared_ptr<ba_obj> eval_expr(Instruction* inst, Frame& frame);
```

第一版重点支持：

- `NameInstruction`
- `NumberInstruction`
- `BinOpInstruction`
- `CallInstruction`

#### 3. 语句执行

准备一个语句执行入口：

```cpp
void exec_inst(Instruction* inst, Frame& frame);
```

第一版重点支持：

- `AssignInstruction`
- `CallInstruction`

#### 4. Terminal 调度

准备一个终结指令执行入口：

```cpp
BasicBlock* exec_terminal(Instruction* term, Frame& frame);
```

第一版重点支持：

- `IfInstruction`
- `JumpInstruction`
- `ReturnInstruction`

#### 5. Block 调度循环

从 `Function::entry_block()` 开始：

1. 顺序执行普通指令
2. 执行 `terminal`
3. 跳转到下一个 block 或返回

### 运行时桥接

为了让 IR 层保持简单，建议准备两个桥接入口：

#### 二元运算桥接

```cpp
std::shared_ptr<ba_obj> eval_binop(BinOpInstruction::Type op,
                                   std::shared_ptr<ba_obj> lhs,
                                   std::shared_ptr<ba_obj> rhs);
```

#### 函数调用桥接

```cpp
std::vector<std::shared_ptr<ba_obj>> eval_call(
    const std::string& name,
    const std::vector<std::shared_ptr<ba_obj>>& in_args);
```

### 为什么值得做

第一版 IR 解释器在“执行动作”上会和 AST 解释器相似，但它的载体已经不同：

- AST 解释器执行的是语法树
- IR 解释器执行的是显式 `BasicBlock + terminal + CFG`

这会为后续能力打基础：

- profile
- 热点识别
- block 级优化
- JIT
- deopt / fallback

### 推荐实现顺序

1. 定义 `Frame`
2. 实现 `eval_expr`
3. 实现 `exec_inst`
4. 实现 `exec_terminal`
5. 跑通 `test/simple_demo.m`

## TODO

### 近期重点

1. 设计最小 `SymbolTable`
   - 第一版先按名字管理变量和函数符号
   - 先支持局部变量查找，再逐步接入 builtin/函数名查找
2. 设计最小 `Frame`
   - 绑定当前 `Function`
   - 保存局部变量、输入参数、返回值和执行上下文
   - 作为一次函数调用的运行时栈帧
3. 实现最小 IR 解释器骨架
   - 顺序执行 `BasicBlock` 内的普通指令
   - 调度 `terminal`
   - 能跑通当前 `simple_demo.m`
4. 逐步补齐 IR 节点的执行支持
   - `NameInstruction`
   - `NumberInstruction`
   - `BinOpInstruction`
   - `AssignInstruction`
   - `CallInstruction`
   - `IfInstruction`
   - `JumpInstruction`
   - `ReturnInstruction`

### 运行时对接

1. 复用 `ba_obj` 作为统一运行时值类型
2. 对接 `/opt/Baltamatica/lib` 下的运行时库
3. 建立二元运算和函数调用的桥接层
