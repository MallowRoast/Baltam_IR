# TODO

## M-script 适配结果

仅记录当前已经接入 `test/test_test*.cpp` 回归测试入口的 m-script。

| 脚本 | 测试入口 | 当前结果 | 说明 |
| --- | --- | --- | --- |
| `test0` | `test_test0` | 已通过 | 已接入运行时回归测试。 |
| `test1` | `test_test1` | 已通过 | 已接入运行时回归测试。 |
| `test1_2` | `test_test1_2` | 已通过 | 已接入运行时回归测试。 |
| `test1_3` | `test_test1_3` | 已通过 | 已接入运行时回归测试。 |
| `test1_4` | `test_test1_4` | 已通过 | 已接入运行时回归测试。 |
| `test1_5` | `test_test1_5` | 已通过 | 已接入运行时回归测试。 |
| `test2` | `test_test2` | 已通过 | 已接入运行时回归测试。 |
| `test2_1` | `test_test2_1` | 已通过 | 已补齐“多返回值直接写入切片左值”的 lowering 支持。 |
| `test2_2` | `test_test2_2` | 已通过 | 已接入运行时回归测试。 |
| `test3` | `test_test3` | 未通过 | 已补测试入口，但当前解释器不会把用户变量同步回运行时符号表，导致 `save(mat_path, 'a', 'b')` 这类按变量名从 runtime 符号表取值的 builtin 在运行时找不到 `a` / `b`。 |
| `test4` | `test_test4` | 已通过 | 已补齐 `varargin` / `nargin`、cell 展开和 cell 写回相关回归。 |
| `test4_2` | `test_test4_2` | 已通过 | 已补齐 `varargout` / `nargout` 回归，并修正表达式位置 builtin 的输出个数按调用点请求值执行。 |
| `test4_3` | `test_test4_3` | 已通过 | 已接入函数实参 alias / 按值返回语义回归。 |

## `test3` 当前阻塞点

- 当前解释器只维护自己的执行状态和值表，没有把用户变量写回运行时符号表。
- `save` / `load` / `clear` 这类 builtin 带有“按变量名访问 runtime 工作区”的语义，不能只靠 SSA 值表执行。
- 因此 `test3` 不是 lowering 失败，而是 runtime 工作区语义还没有桥接完成。

## 后续待办

- 给解释器补一层“运行时符号表同步”机制，至少覆盖 `save` / `load` / `clear` 这类按名字访问变量的 builtin。
- 评估哪些 builtin 必须依赖 runtime 工作区，哪些可以继续只走当前解释器内部值表。
- 后续考虑接入 REPL。
- 后续考虑接入 `io_manager`，让解释器执行过程中产生的运行时输出信息能够被前端/宿主正常接收。

## 备注

- 当前命令行 `build/` 目录在这台机器上还存在独立的 GCC ICE 噪音；这和 `test3` 的真实语义失败不是同一问题。
- `test3` 的真实失败原因已经在可执行的 `cmake-build-debug/test/test_test3` 上复现并确认。
