# Switch Lowering 设计

## 目标

本文记录当前 `switch / case / otherwise` lowering 方案。方案参考 `dev`
分支中旧 non-SSA lowering 的控制流形状，但会落到当前
`src/ir/ir_lowering.{h,cpp}` 的 `BasicBlock / BranchInst / GotoInst / CallInst`
模型中。

第一阶段覆盖 `test/m/test4/test4.m` 这类脚本用例：

```matlab
x = 2;
y = 0;

switch x
    case 1
        y = 10;
    case {2, 3}
        y = 20;
    otherwise
        y = -1;
end
```

## Matlab 语义要点

- `switch` 表达式只求值一次。
- `case` 从上到下顺序匹配，命中第一项后只执行该 `case` 的语句体。
- `case {a, b, c}` 表示任一元素匹配即可进入该 `case`。
- `otherwise` 可选；没有任何 `case` 命中且没有 `otherwise` 时，直接跳到
  `switch.end`。
- `case` 之间没有 C 语言式 fallthrough；每个 case body 正常结束后统一跳到
  `switch.end`。

## 参考 dev 分支

`dev` 分支旧 lowering 的核心策略是：

- 先 lower `switch` 表达式，保存为 `switch_value`
- 为整个语句创建一个 `switch.end`
- 对每个 `case` 生成一个条件判断块和一个 `switch.case` body 块
- 未命中当前 `case` 时跳到下一个 `switch.next`
- case body 普通结束后跳到 `switch.end`
- 最后一个 `switch.next` 进入 `otherwise`，或在没有 `otherwise` 时直接跳到
  `switch.end`

当前实现不直接搬旧代码，只复用这个 CFG 形状。

## CFG 形状

对两个 `case` 加一个 `otherwise` 的示例，目标 CFG 为：

```text
current
  -> switch.dispatch

switch.dispatch
  %switch_value = lower switch expr
  %match0 = match(%switch_value, case0)
  br %match0, label %switch.case, label %switch.next

switch.case
  ...
  br label %switch.end

switch.next
  %match1 = match(%switch_value, case1)
  br %match1, label %switch.case.1, label %switch.next.1

switch.case.1
  ...
  br label %switch.end

switch.next.1
  br label %switch.otherwise

switch.otherwise
  ...
  br label %switch.end

switch.end
  -> 后续 continuation
```

如果没有 `otherwise`，最后一个 `switch.next` 直接 synthetic goto 到 `switch.end`。

## Block 命名

建议使用以下 label：

- `switch.dispatch`：求值并保存 switch 表达式，只执行一次
- `switch.case`：当前 case 的 body 块
- `switch.next`：当前 case 未命中后进入下一项判断的块
- `switch.otherwise`：otherwise body 块
- `switch.end`：所有 case / otherwise 的统一出口

同一 `CodeUnit` 中重复 label 继续依赖 printer / DOT printer 的 `.1 / .2` 后缀规则区分。

## 匹配条件

### 普通 case

普通 case：

```matlab
case 1
```

lower 为一次内部匹配 helper：

```text
%rhs = const 1
%match = call @internal.switch_match(%switch_value, %rhs)
br %match, label %switch.case, label %switch.next
```

这里建议使用 `CallInst(dispatch_type = Internal)`，callee 裸名为 `switch_match`，
文本 IR 打印为 `call @internal.switch_match(...)`。不要直接 lower 为用户级 `eq`，
原因是 Matlab `switch` 匹配语义不是简单的数值 `==`：

- 字符串 / char 需要按 Matlab 的匹配规则处理
- cell case 需要逐元素匹配
- 后续对象、枚举或更复杂类型可能有专门规则

### cell case

cell case：

```matlab
case {2, 3}
```

可以有两种实现路线：

1. lowering 展开为多个 `internal.switch_match`，再用 `internal.or` 或
   `BinaryInst(Or, dispatch_type = Internal)` 合并。
2. 保留 cell 表达式整体，交给 `internal.switch_match` 识别和遍历。

参考 `dev` 分支，第一阶段建议采用路线 1：

```text
%rhs0 = const 2
%m0 = call @internal.switch_match(%switch_value, %rhs0)
%rhs1 = const 3
%m1 = call @internal.switch_match(%switch_value, %rhs1)
%match = internal.or %m0, %m1
br %match, label %switch.case.1, label %switch.next.1
```

这样 CFG 与条件值更透明，也方便 smoke test 检查 cell case 确实展开成多个匹配项。

## Lowering 流程

`IRLowerer` 中新增：

```cpp
void lower_switch_stmt(const std::shared_ptr<switch_flow>& switch_node);
ValueId build_switch_match_condition(ValueId switch_value, const ast_ptr& case_value);
```

主流程：

1. 校验 `switch_node->expr()` 和 `switch_node->cases()`。
2. 创建 `switch.dispatch`、`switch.end`。
3. 当前 open block synthetic goto 到 `switch.dispatch`。
4. 在 `switch.dispatch` lower switch 表达式，得到 `switch_value`。
5. 遍历 `cases()->branch`：
   - `node_case`：创建 `switch.case`、`switch.next`
   - `node_otherwise`：暂存 otherwise 节点，最后处理
6. 在当前 check block 中构造匹配条件并生成 `BranchInst`。第一个 check block 是
   `switch.dispatch`，后续 check block 是前一个 case 失败边进入的 `switch.next`。
7. 每个 `switch.case` body lower 完成后，如果当前 block 仍 open，synthetic goto
   到 `switch.end`。
8. 所有 case 失败后：
   - 有 `otherwise`：进入 `switch.otherwise`
   - 无 `otherwise`：goto `switch.end`
9. `switch.otherwise` body lower 完成后，如果当前 block 仍 open，synthetic goto
   到 `switch.end`。
10. 最后把插入点设到 `switch.end`，后续语句继续从这里 lower。

## 与 break / continue 的关系

Matlab 的 `switch` 本身不是循环，不应建立新的 loop context：

- `switch` 内部的 `break / continue` 仍然只对外层最近循环生效。
- 如果 `switch` 不在循环内，`break / continue` 仍应按当前规则报错。

因此 `lower_switch_stmt()` 不应使用 `ScopedLoopContext`。

## 测试覆盖

第一阶段输入：

- `test/m/test4/test4.m`
- `test/m/test4/test4_1.m`
- `test/m/test4/test4_2.m`
- `test/m/test4/test4_3.m`

已接入：

- `test/smoke_test/syntax/test4_smoke.cpp`
- `test/smoke_test/syntax/test4_1_smoke.cpp`
- `test/smoke_test/syntax/test4_2_smoke.cpp`
- `test/smoke_test/syntax/test4_3_smoke.cpp`
- `test/smoke_test/CMakeLists.txt`
- `CFG_DOT_CASES` 中的 `test4/test4`、`test4/test4_1`、`test4/test4_2`、
  `test4/test4_3`

`test4_smoke` 应重点检查：

- 生成 `switch.dispatch / switch.case / switch.next / switch.otherwise / switch.end`
- `switch` 表达式只 lower 一次
- `case {2, 3}` 生成两次 `internal.switch_match` 并合并为一个条件
- 每个 case body 正常结束后 synthetic goto 到 `switch.end`
- 没有 C 风格 fallthrough

`test4_smoke` 同时检查无 `otherwise`：

- 不生成额外 `switch.otherwise`
- 所有 `case` 失败后，最后一个 `switch.next` 直接进入对应 `switch.end`

`test4_1_smoke` 重点检查嵌套 `switch`：

- 内外两层生成独立的 `switch.dispatch / switch.end`
- 内层 `switch.end` 回到外层 case 的后续语句
- 内外两层重复 block label 通过 printer 后缀区分

`test4_3_smoke` 重点检查 `switch` 包裹循环：

- `switch case` 中可以分别 lower `for` 和 `while`
- `switch` 不建立 loop context，循环内部 `break / continue` 仍然命中最近循环
- `for.end / while.end` 正常结束后回到外层 `switch.end`

## 当前暂不覆盖

- switch 内嵌 loop 的专项测试
- 字符串、char、cell array 之外更复杂类型的匹配语义
- `otherwise` 缺省时的专门测试
