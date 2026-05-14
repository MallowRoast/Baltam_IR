# Switch Lowering 设计

本文记录当前 `switch / case / otherwise` lowering 的实际形状。

## 语义要点

- `switch` 表达式只求值一次。
- `case` 从上到下顺序匹配，命中第一项后只执行该 case body。
- `case {a, b, c}` 表示任一元素匹配即可进入该 case。
- `otherwise` 可选；没有任何 case 命中且没有 otherwise 时，直接跳到 `switch.end`。
- case 之间没有 C 语言式 fallthrough；每个 case body 正常结束后统一跳到 `switch.end`。
- Matlab 的 `switch` 本身不是循环，不建立新的 loop context；内部 `break / continue` 仍然
  只对外层最近循环生效。

## CFG 形状

两个 case 加一个 otherwise 的典型形状：

```text
current
  -> switch.dispatch

switch.dispatch
  %switch_value = lower switch expr
  %match0 = internal.switch_match(%switch_value, case0)
  br %match0, label %switch.case, label %switch.next

switch.case
  ...
  goto switch.end

switch.next
  %match1 = internal.switch_match(%switch_value, case1)
  br %match1, label %switch.case.1, label %switch.next.1

switch.case.1
  ...
  goto switch.end

switch.next.1
  goto switch.otherwise

switch.otherwise
  ...
  goto switch.end

switch.end
  -> 后续语句
```

没有 otherwise 时，最后一个 `switch.next` 直接 synthetic goto 到 `switch.end`。

## Block 命名

当前固定使用这些 label：

- `switch.dispatch`：求值并保存 switch 表达式，只执行一次
- `switch.case`：case body
- `switch.next`：当前 case 未命中后进入下一项判断
- `switch.otherwise`：otherwise body
- `switch.end`：统一出口

同一 `CodeUnit` 中重复 label 由 printer / DOT printer 加 `.1 / .2` 后缀区分。

## 匹配条件

普通 case：

```matlab
case 1
```

lower 为内部匹配 helper：

```text
%rhs = const 1
%match = call @internal.switch_match(%switch_value, %rhs)
br %match, label %switch.case, label %switch.next
```

这里使用 `CallInst(dispatch_type = Internal)`，callee 裸名为 `switch_match`，文本 IR 打印为
`call @internal.switch_match(...)`。不直接 lower 成用户级 `eq`，因为 Matlab `switch`
匹配语义不等同于简单数值比较。

cell case：

```matlab
case {2, 3}
```

当前展开为多个 `internal.switch_match`，再用 internal 逻辑合并：

```text
%rhs0 = const 2
%m0 = call @internal.switch_match(%switch_value, %rhs0)
%rhs1 = const 3
%m1 = call @internal.switch_match(%switch_value, %rhs1)
%match = call @internal.or(%m0, %m1)
br %match, label %switch.case.1, label %switch.next.1
```

## Lowering 流程

`IRLowerer` 中的相关入口：

```cpp
void lower_switch_stmt(const std::shared_ptr<switch_flow>& switch_node);
ValueId build_switch_match_condition(ValueId switch_value, const ast_ptr& case_value);
```

主流程：

1. 校验 `switch_node->expr()` 和 `switch_node->cases()`。
2. 创建 `switch.dispatch` 和 `switch.end`。
3. 当前 open block synthetic goto 到 `switch.dispatch`。
4. 在 `switch.dispatch` lower switch 表达式，得到 `switch_value`。
5. 遍历 case 列表；普通 case 创建 `switch.case` 和 `switch.next`，otherwise 暂存到最后。
6. 在当前 check block 中构造匹配条件并生成 `BranchInst`。
7. 每个 case body lower 完成后，如果当前 block 仍 open，synthetic goto 到 `switch.end`。
8. 所有 case 失败后，有 otherwise 则进入 `switch.otherwise`，否则 goto `switch.end`。
9. otherwise body lower 完成后，如果当前 block 仍 open，synthetic goto 到 `switch.end`。
10. 最后把插入点设到 `switch.end`，后续语句继续从这里 lower。

## 测试覆盖

当前 smoke 覆盖：

- `test4`：普通 case、cell case、otherwise、无 otherwise
- `test4_1`：嵌套 switch
- `test4_2`：for / while 中嵌套 switch
- `test4_3`：switch case 包裹 for / while，循环内 `break / continue` 命中最近循环

测试文件位于：

- `test/m/test4/`
- `test/smoke_test/syntax/test4*_smoke.cpp`

## 未覆盖范围

- 字符串、char、对象、枚举等完整匹配语义
- 更复杂 cell / array case 的运行时错误细节
- 与 `try / catch`、`return` 等控制流组合的完整覆盖
