# TODO

## M-script 适配结果

当前表格只记录已经接入 `test/test_test*.cpp` 回归测试入口的 m-script。

| 脚本 | 测试入口 | 当前结果 | 说明 |
| --- | --- | --- | --- |
| `test0` | `test_test0` | 已通过 | 基础表达式与脚本输出回归。 |
| `test1` | `test_test1` | 已通过 | 函数入参、返回值、分支、循环、子函数调用回归。 |
| `test1_2` | `test_test1_2` | 已通过 | 已接入运行时回归测试。 |
| `test1_3` | `test_test1_3` | 已通过 | 已接入运行时回归测试。 |
| `test1_4` | `test_test1_4` | 已通过 | 已接入运行时回归测试。 |
| `test1_5` | `test_test1_5` | 已通过 | 已接入运行时回归测试。 |
| `test2` | `test_test2` | 已通过 | `paren_set` 与表达式位置圆括号取值分派回归。 |
| `test2_1` | `test_test2_1` | 已通过 | 已补齐“多返回值直接写入切片左值”的 lowering 支持。 |
| `test2_2` | `test_test2_2` | 已通过 | 已接入运行时回归测试。 |
| `test3` | `test_test3` | 未通过 | `save/load/clear` 依赖 runtime 工作区按名字读写变量，当前解释器只维护 SSA 值表。 |
| `test4` | `test_test4` | 已通过 | `varargin` / `nargin`、cell 展开和 cell 写回回归。 |
| `test4_2` | `test_test4_2` | 已通过 | `varargout` / `nargout` 回归。 |
| `test4_3` | `test_test4_3` | 已通过 | 函数实参 alias / 按值返回语义回归。 |
| `test5` | `test_test5` | 已通过 | 已接入运行时回归测试。 |
| `test6` | `test_test6` | 已通过 | 已接入运行时回归测试。 |
| `test7` | `test_test7` | 未通过 | 当前失败为 `non-SSA lower 目前只支持名字左值赋值。`，短路逻辑和复杂左值组合的 lowering 还没补齐。 |
| `test7_short` | `test_test7_short` | 已通过 | 短路逻辑基础路径回归。 |
| `test8` | `test_test8` | 未通过 | 当前失败为 `non-SSA lower 暂不支持该表达式节点: node_magic_end`。 |
| `test9` | `test_test9` | 已通过 | 基础函数控制流与返回值回归。 |
| `test9_2` | `test_test9_2` | 已通过 | `for` 循环中的 `continue / break` 回归。 |
| `test9_3` | `test_test9_3` | 已通过 | 嵌套 `for / while` 与 `continue / break` 回归。 |
| `test9_4` | `test_test9_4` | 已通过 | `if / elseif / else` 链回归。 |

## 当前阻塞点

- `test3`
  解释器需要补 runtime 工作区同步，至少覆盖 `save` / `load` / `clear` 这类按变量名访问工作区的 builtin。
- `test7`
  需要补齐复杂左值赋值的 lowering，尤其是短路表达式与 setter 组合的路径。
- `test8`
  需要给 `node_magic_end` 建立 canonical lowering。

## 后续待办

- 评估哪些 builtin 必须依赖 runtime 工作区，哪些可以继续只走解释器内部值表。
- 在 `test7` / `test8` 打通后继续向 `test10+` 推进 m-script 适配。
- 后续考虑接入 REPL 与 `io_manager`。
