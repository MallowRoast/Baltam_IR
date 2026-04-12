# TODO

## 当前状态

- 截至当前工作区，完整 `ctest` 结果是 `54/55` 通过。
- 已接入 `ctest` 的 m-script 回归里，当前唯一失败项是 `test3`。
- `test7` 已打通，不再是当前阻塞项。

## 已接入的 m-script 回归

| 测试 | 当前状态 | 说明 |
| --- | --- | --- |
| `test0`, `test1`, `test1_2`, `test1_3`, `test1_4`, `test1_5`, `test2`, `test2_1`, `test2_2` | 通过 | 基础脚本执行、调用和 SSA 执行链已覆盖。 |
| `test4`, `test4_2`, `test4_3`, `test5`, `test6` | 通过 | 早期控制流和脚本回归稳定。 |
| `test7`, `test7_short` | 通过 | 短路逻辑与复杂左值写回组合路径已打通。 |
| `test8` | 通过 | `end` lowering 与运行时执行链已打通。 |
| `test9`, `test9_2`, `test9_3`, `test9_4` | 通过 | 相关脚本回归稳定。 |
| `test30`, `test30_2`, `test31`, `test32` | 通过 | COW、alias、空脚本场景已接入。 |
| `test33`, `test33_1`, `test33_2`, `test33_3`, `test33_4` | 通过 | `zeros`、`sum`、逐元素运算和 builtin 组合已接入。 |
| `test33_6` | 通过 | 当前以裁剪版通过，`polyder`、`polyint`、`fft` 相关子测试已暂时注释。 |
| `test34`, `test35`, `test36`, `test37` | 通过 | 已覆盖 `nargin/nargout`、`end`、占位输出、多返回值。 |
| `test38`, `test39`, `test44`, `test45` | 通过 | 已覆盖 `global`、global struct / paren / cell 写回。 |
| `test40`, `test41`, `test42`, `test43` | 通过 | 相关运行时脚本回归稳定。 |
| `test3` | 未通过 | `save / load / clear` 依赖按源码变量名读写当前工作区；当前解释器只有 SSA 值表和 `global` 工作区，没有脚本级 named workspace。 |

## 尚未接入 `ctest` 的 test 目录

这些目录下有 `.m` 用例，但当前还没有独立的 `test_testXX.cpp` 回归入口，状态应视为“未验证”，不是“失败”：

- `test10`
- `test11`, `test11_2`, `test11_3`
- `test12`, `test12_1`
- `test13`, `test13_2`, `test13_3`, `test13_5`
- `test14`, `test14_1`, `test14_2`
- `test15`, `test16`, `test17`, `test18`
- `test20`
- `test22`, `test23`, `test24`, `test25`
- `test26`, `test26_2`, `test27`, `test28`, `test29`
- `test_function_prescan`
- `test_return_lowering`
- `test_short_circuit_lowering`

## 当前阻塞项

| 项目 | 当前状态 | 说明 |
| --- | --- | --- |
| `test3` | 未通过 | 需要给解释器补“当前脚本工作区”的按名字读写能力，至少覆盖 `save / load / clear`。 |

## 新增 TODO

- 设计并接入脚本级 named workspace，让 `save / load / clear` 能按源码变量名读写当前执行帧，而不只是依赖 SSA 值表。
- 思考如何接入 runtime 的 REPL，支持拿到 runtime 侧的输出信息：
  需要明确 stdout / stderr、`disp` / `fprintf`、warning / error、logger 信息应如何回传到解释器或测试层。
- 在 `test3` 修复后，继续把 `test10` 到 `test29` 这批目录逐步接入 `ctest`。
- 维持文档、回归入口和仓库真实状态同步，避免 `todo.md` 再出现过时状态。
