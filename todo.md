# TODO

## 当前阻塞项

| 项目 | 当前状态 | 说明 |
| --- | --- | --- |
| `test3` | 未通过 | `save / load / clear` 依赖 runtime 工作区按名字读写变量，当前解释器只维护 SSA 值表。 |
| `test7` | 未通过 | 复杂左值赋值与短路逻辑组合路径的 lowering 还没补齐。 |

## 最近完成

- `test8`
  `node_magic_end` 的 lowering 与运行时执行链已经打通。

## 下一步

- 评估哪些 builtin 必须依赖 runtime 工作区，哪些仍可只走解释器内部值表。
- 在 `test7` 打通后继续向更多 m-script 回归推进。
- 维持构建与测试流水线，避免文档状态再次和仓库实际结果脱节。
