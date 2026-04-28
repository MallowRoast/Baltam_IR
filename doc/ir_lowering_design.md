# IR Lowering 设计

## 目标

本文只记录当前 `src/ir/ir_lowering.{h,cpp}` 已经实现的 lowering 分层和职责，不再保留早期“骨架-only lowering”的历史计划。

当前 lowering 已经支持：

- 从 `.m` 文件直接 parse 并 lower
- 从 parser 产出的 `pcdata[]` 直接 lower
- 生成可验证、可打印、带源码注释的 `IR`
- 文件内 `local` 函数的最小支持

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

- 创建 `MFileUnit`
- 为每个 parser 单元创建 `ScriptUnit` 或 `FunctionUnit`
- 在 lowering 前先识别入口单元和文件内 local `FunctionUnit`
- 为函数从 `mFileFunc` AST 预声明参数 slot 和返回值 slot
- 为每个 unit 创建 `entry` 基本块
- lower `test0 / test0_1 / test1 / test1_1` 所需的最小语句/表达式子集：
  - 简单赋值
  - 数值字面量
  - 名字读取
  - 一元运算
  - 二元运算
  - 名字形式的圆括号应用
  - 带输出参数的圆括号应用语句
  - 文件内 `local` 函数的最小分派
  - `if / else`
  - 显式 `return`
  - 隐式 `return`

### script 与 function 的区别

- `script` 名字访问固定 lower 成 `LoadWorkspaceInst` / `StoreWorkspaceInst`
  打印时分别显示为 `load_env` / `store_env`
- `function` 名字访问固定 lower 成 `LoadSlotInst` / `StoreSlotInst`
- `script` 中的 `A(...)` 先保留为 `ApplyInst`
- `function` 中的 `A(...)` 会根据 lowering 期名字绑定表分派：
  若 `A` 尚未绑定为变量，且命中文件内 `local` 函数，则直接 lower 成
  `CallInst(Local)`，打印时显示为 `call_local @A(...)`；
  若 `A` 尚未绑定为变量，且未命中文件内 `local` 函数，则直接 lower 成
  `CallInst(Direct)`；
  若 `A` 已绑定为 slot 名字，则保留为 `ApplyInst`
- `MFileUnit` 当前会记录入口单元之外的 local `FunctionUnit`，供 function lowering
  使用；script 主体当前仍不会因为 local 函数存在而把 `apply` 收敛成 `call`
- `function` 中的一元 / 二元运算也会尝试按 Matlab 同名规则命中文件内 local 函数：
  例如 `+` 对应 `plus`，一元 `-` 对应 `uminus`。若这些名字未被局部变量遮蔽，
  则表达式会直接 lower 成 `call_local`；若已经被局部变量遮蔽，则回退为普通
  `UnaryInst` / `BinaryInst`
- `WorkspaceHandle` hidden slot 只出现在 `ScriptUnit`，并在第一次脚本名字读写时按需创建；
  当前脚本环境槽位名字采用 `<script_name>_env`

### 源码位置

`IRLowerer` 会读取源文件文本，并把 AST 节点的 `location(begin/end line,column)` 映射成 `SourceSpan(begin_offset,end_offset)`。

当前这层映射主要服务于：

- `ir_print` 行尾源码注释
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
- `IRLowerer` 负责 AST 语义决策、名字分类和源码位置桥接

block 的创建与 `entry` 指定现在由 `CodeUnit` 自身完成，builder 只维护当前插入点。

## 当前 smoke test

当前端到端闭环测试是：

- `test/smoke_test/test0_smoke.cpp`
- `test/smoke_test/test0_1_smoke.cpp`
- `test/smoke_test/test1_smoke.cpp`
- `test/smoke_test/test1_1_smoke.cpp`

它会：

- parse `test/m/test0/test0.m` / `test/m/test0/test0_1.m` / `test/m/test1/test1.m` /
  `test/m/test1/test1_1.m`
- build 成 `IR` 并检查结构完整、没有 `Error` 诊断
- 校验各自的核心侧重点是否 lower 正确：
  - `test1`：脚本里即使定义了 local `sin`，主体中的 `sin(a)` 仍保留为 `apply`
  - `test1_1`：函数里 `sin(a)` / `-a` 可命中 local `sin` / `uminus`，而 `plus = 1`
    和 `cos = 1` 又会分别遮蔽 local `plus` / `cos`
- 调用 `ir_print` 打印文本 IR
- 校验关键打印结果与源码行号注释

## 当前未覆盖范围

当前 lowering 仍未覆盖完整 Matlab 语义，典型缺口包括：

- `for / while / break / continue`
- `switch`
- `try / catch`
- 嵌套函数、匿名函数、闭包
- `global / persistent`
- `eval` / 动态名字解析
- 更复杂的 `A(...)` 消歧

因此这层 lowering 当前仍是“最小可闭环实现”，但已经不是只生成骨架的早期版本。
