# M 函数工作区中的静态 slot 与动态 env

本文是历史讨论记录的归档入口。原文围绕 `slot`、workspace/env、`eval` 和脚本调用语义展开；
其中仍有效的结论已经合并到正式设计文档中，后续维护应优先修改这些文档。

核心结论保留如下：

> M 函数的工作区不应只理解成静态 `slot` frame。它还需要一层动态 `env`，承载 `eval`、
> 脚本、`assignin`、`load` 等机制在运行时创建的名字绑定。用户可见 slot 还需要当前
> binding 是否 live 的状态。
>
> 动态 `env` 不应反向改变函数正文已经静态确定的变量/函数分派。

## 已合并位置

- 函数 Frame、slot binding state、`eval('clear a')` 后的 checked slot 访问：
  [InterpreterContext、CodeObject 与 Frame 设计](./runtime_execution_objects_design.md)
- workspace 作为 `name -> binding` 视图、脚本运行时 target workspace、动态 env observer：
  [M 工作区设计](./workspace_design.md)
- 变量类别、binding liveness、`clear` 对各类变量的影响：
  [M 变量模型设计](./variable_model_design.md)
- `global` / `persistent` 不是普通 frame slot、后续专用节点和 effect：
  [Global / Persistent IR 节点设计](./global_persistent_ir_design.md)

## 保留边界

- `SlotId` 表示静态 layout 身份，不表示当前 activation 中名字一定 live。
- `clear`、`eval('clear x')`、脚本调用或未知动态 binding barrier 可能让用户可见 slot 的
  fast path 失效。
- 脚本的 `ScriptVar` slot 表示脚本文本中的静态名字身份，真实存储位置来自运行时 target
  workspace。
- 动态 env 中的新名字可以被后续动态机制观察，但不会临时扩展 caller 函数的静态 slot 表。
