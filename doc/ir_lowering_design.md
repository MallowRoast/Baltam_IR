# IR Lowering 设计

## 目标

本文只记录当前 `src/ir/ir_lowering.{h,cpp}` 已经实现的 lowering 分层和职责，不再保留早期“骨架-only lowering”的历史计划。

当前 lowering 已经支持：

- 从 `.m` 文件直接 parse 并 lower
- 从 parser 产出的 `pcdata[]` 直接 lower
- 生成可验证、可打印、带源码注释的 `IR`
- 文件内 `local` 函数的最小支持
- 具名函数句柄、匿名函数句柄和 `value_apply`
- 多返回值调用和 `~` 占位输出位
- `if / else`
- `switch / case / otherwise`
- `for / while` 循环和循环内 `break / continue`
- 嵌套循环中的最近一层 loop context 选择

## 公开接口

公开头文件：`src/ir/ir_lowering.h`

当前接口如下：

```cpp
class IRLowerer final {
public:
    IRLowerer() noexcept = default;

    IRBuildResult lower_parsed_units(
        const std::vector<std::shared_ptr<pcdata>>& parsed_units);
};

IRBuildResult parse_and_lower_mfile_to_ir(
    std::string_view filename,
    const ParserOpts& parser_opts = ParserOpts());
```

## 分层方式

### 1. `lower_parsed_units(...)`

这是核心 lowering 入口，直接消费：

```cpp
std::vector<std::shared_ptr<pcdata>>
```

也就是 `bt_ast_interface::parse_mfile()` 的直接输出。

当前约定：

- `MFileUnit::path` 直接复用第一个有效 `pcdata` 的 `filename`
- 每个 `pcdata` 对应一个 `ScriptUnit` 或 `FunctionUnit`
- 脚本单元或函数名等于文件名的单元对应入口单元，其余函数单元可作为文件内 local 函数
- `IRLowerer` 自己维护源码文本和行起始偏移表，用于把 AST `location` 转成 `SourceSpan`

### 2. `parse_and_lower_mfile_to_ir(...)`

这是文件级便利入口，负责：

- 初始化 / 释放 `bt_ast_interface`
- 调用 `parse_mfile()`
- 把 parser 输出继续交给 `IRLowerer`

这样做的意义是：

- 上层已有 `pcdata[]` 时可以直接调用 `lower_parsed_units(...)`
- 需要一条端到端闭环时可以直接从 `.m` 文件进入

## 当前 lowering 语义

当前实现会完成以下工作：

- 创建 `IRModule` 和 `MFileUnit`
- 为每个 parser 单元创建 `ScriptUnit` 或 `FunctionUnit`
- 为匿名函数表达式创建 module 级 `AnonymousFunctionUnit`
- 在 lowering 前先识别入口单元和文件内 local `FunctionUnit`
- 为函数从 `mFileFunc` AST 预声明参数 slot 和返回值 slot
- 为每个 unit 创建 `entry` 基本块
- lower 当前 smoke 语法样例所需的语句/表达式子集：
  - 简单赋值
  - 数值字面量
  - 名字读取
  - 一元运算
  - 二元运算
  - 名字形式的圆括号应用
  - 带输出参数的圆括号应用语句
  - 多返回值调用和 `~` 占位输出位
  - 具名函数句柄 `@name`
  - 匿名函数句柄 `@(args) expr`
  - 值圆括号应用 `value_apply`
  - 文件内 `local` 函数的最小分派
  - `if / else`
  - `switch / case / otherwise`，当前采用 `switch.case /
    switch.body / switch.otherwise / switch.end` CFG 形状，详见
    [switch_lowering_design.md](./switch_lowering_design.md)
  - `for` 循环，当前采用 `for.preheader / for.header / for.body / for.latch / for.end`
    五块 CFG 形状，详见 [loop_lowering_design.md](./loop_lowering_design.md)
  - `while` 循环，当前采用 `while.header / while.body / while.end`
    三块 CFG 形状
  - 显式 `return`
  - 隐式 `return`

### script 与 function 的区别

- `script` 中静态出现的变量会创建 `ScriptVar` slot，名字访问 lower 成 `LoadSlotInst` /
  `StoreSlotInst`，文本 IR 打印为 `load` / `store`
- `function` 名字访问同样 lower 成 `LoadSlotInst` / `StoreSlotInst`；参数、返回值、局部变量
  分别通过 `Arg`、`Ret`、`Local` tag 区分
- `script` 中的 `A(...)` 先保留为 `ApplyInst`
- `function` 中的 `A(...)` 会根据 lowering 期名字绑定表分派：
  若 `A` 尚未绑定为变量，则直接 lower 成
  `CallInst(callee_kind = Direct, dispatch_type = Dynamic)`；
  若 `A` 已绑定为 slot 名字，则先 `LoadSlotInst` 读取当前变量值，再 lower 成
  `ValueApplyInst`。`ValueApplyInst` 表示 base 已经是 `ValueId`，但尚未分派为函数句柄
  调用或圆括号取值。
- `MFileUnit` 当前会记录入口单元之外的 local `FunctionUnit`，但基础 lowering 不会因为
  local 函数存在就把调用静态绑定成 `MFunction`。后续名字解析 pass 需要同时考虑
  `import`、`private`、路径和遮蔽规则，再决定是否把动态 direct call 收敛成 `call mfunc`。
- `function` 中的一元 / 二元运算在基础 lowering 中保留为普通 `UnaryInst` /
  `BinaryInst`，不因为存在同名 local 函数就提前静态分派。后续 pass 可以在完整名字解析
  稳定后再收敛到具体函数或 builtin。
- lowering 阶段的静态名字查询统一走两个入口：
  - `lookup_var(name)` 查询当前 lowering 已知的变量表，当前底层来自 `IRLowerer` 按
    `CodeUnit` 保存的 `name -> Slot` side table。这张表只属于 lowering 期语义状态，
    不由 `IRBuilder` 持有，也不进入最终 IR。
- `@name` 应 lower 成 `CreateNamedFunctionHandleInst`。该节点表达的是“构造具名函数句柄”，
  而不是普通字符串常量：
  - `resolution_mode = runtime`：基础 lowering 默认形态。运行到 `f = @name` 时查询一次，
    查询结果进入运行时句柄对象：查到则句柄绑定目标，查不到则句柄保持 unresolved，
    后续调用该句柄时再按名字查询。
  - `resolution_mode = static`：前置名字解析 pass 已经确定目标，运行时直接构造已绑定
    句柄，不再查询。当前允许的静态目标类别是 `builtin` 和 `mfunction`。
  - 该指令结果类型固定为 `function_handle scalar`。
- `@(args) expr` 会 lower 成 `CreateAnonymousFunctionHandleInst`，并在
  `IRModule::anonymous_functions` 中创建 `AnonymousFunctionUnit`。捕获变量在构造点先
  通过外层变量 slot lower 成 `ValueId`，匿名函数体内部通过自己的 `Capture` slot 读取捕获值。
- 多返回值调用按左值列表长度生成结果位；`~` 占位输出位保留位次，但不创建真实
  `ValueId`，printer 显示为 `[]`。

### 源码位置

`IRLowerer` 会读取源文件文本，并把 AST 节点的 `location(begin/end line,column)` 映射成 `SourceSpan(begin_offset,end_offset)`。

当前这层映射主要服务于：

- `ir_print` 行尾源码行号注释；默认只打印 `line N` / `line N-M`，需要源码片段时使用
  `--source-full`
- builder / lowering 诊断定位

## 空主体与诊断

当前 lowering 对异常或不完整输入采取保守处理：

- `parsed_units` 为空时直接报错
- 工作区列表中出现空项时发 warning 并跳过
- unit 没有可 lower 的主体 AST 时发 warning，并跳过主体 lowering
- 若当前 block 末尾没有 terminator，则统一补一个隐式 `return`

这意味着即使主体为空，lowering 仍然会尽量生成结构完整的空 unit，而不是中途退出并留下不完整 CFG。

## 与 `IRBuilder` 的关系

`IRLowerer` 不直接操作 `MFileUnit / Slot / Instruction` 细节，而是通过 `IRBuilder` 落地：

- `IRBuilder` 负责对象创建、插入点管理和 CFG 边维护
- `IRLowerer` 负责 AST 语义决策、名字分类、lowering 期名字绑定和源码位置桥接

block 的创建与 `entry` 指定现在由 `CodeUnit` 自身完成，builder 只维护当前插入点。

## 当前 smoke test

当前端到端闭环测试是：

- 语法样例：`test/smoke_test/syntax/*_smoke.cpp`
- 功能样例：`test/smoke_test/feature/*_smoke.cpp`

它会：

- build 成 `IR` 并检查结构完整、没有 `Error` 诊断
- 通过 verifier 检查 parent 指针、CFG 边、slot/value 引用等结构约束
- 校验各自的核心侧重点是否 lower 正确：
  - `test1`：脚本里即使定义了 local `sin`，主体中的 `sin(a)` 仍保留为 `apply`
  - `test1_1`：函数里 `sin(a)` / `-a` 可命中 local `sin` / `uminus`，而 `plus = 1`
    和 `cos = 1` 又会分别遮蔽 local `plus` / `cos`
  - `test2`：简单 `for i = 1:10` 生成五块 loop CFG，其中 `colon` lower 为非
    internal 的普通 `call`，`foreach_init` 和 `foreach_iterate` lower 为
    可静态确定的 `internal.foreach_init` / `internal.foreach_iterate` 调用
  - `test2_1`：循环体内 `continue` 跳到 `for.latch`，`break` 跳到 `for.end`
  - `test2_2`：嵌套 `for` 生成两套独立 loop CFG 和 internal 迭代状态
  - `test2_3`：嵌套 `for` 中内层 `continue` 和外层 `break` 分别命中最近一层
    loop context 的正确目标
  - `test3`：简单 `while` 生成三块 loop CFG
  - `test3_1`：`while` 循环体内的 `continue / break` 分别跳到
    `while.header / while.end`
  - `test3_2`：`for / while` 混合嵌套后，各自的 end/header/latch 回到外层循环的正确延续块
  - `test3_3`：混合嵌套中的 `break / continue` 始终选择最近一层循环目标
  - `test4`：简单 `switch / case / otherwise` 和无 `otherwise` 的 `switch`
  - `test4_1`：`switch` 内嵌 `switch` 时，内层 `switch.end` 回到外层 case 后续语句
  - `test4_2`：`for / while` 内嵌 `switch` 时，`switch.end` 回到外层
    `for.latch / while.header`
  - `test4_3`：`switch case` 包裹 `for / while` 时，循环内 `break / continue` 命中
    最近循环，循环正常结束后回到 `switch.end`
  - `test5_1`：具名函数句柄、匿名函数句柄、捕获值和 `value_apply`
  - `test7`：多返回值签名、多结果 `call` 和 `~` 占位输出位

syntax smoke test 不再校验文本 IR 的显示格式，避免缩进、block 注释、source comment 等
printer 调整影响 lowering 语义测试。文本 IR 的 CLI 可用性由 `ir_print_cli_smoke` 单独覆盖。

## 当前未覆盖范围

当前 lowering 仍未覆盖完整 Matlab 语义，典型缺口包括：

- `switch` 的字符串、对象、枚举等完整匹配语义
- `try / catch`
- 嵌套函数和完整闭包 runtime
- `global / persistent`
- `eval` / 动态名字解析
- 更复杂的 `A(...)` 消歧

因此这层 lowering 当前仍是“最小可闭环实现”，但已经不是只生成骨架的早期版本。
