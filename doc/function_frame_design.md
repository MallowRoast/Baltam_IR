# M 函数栈帧设计

`FunctionFrame` 不再作为脱离执行上下文和代码对象的独立方案维护。当前正式设计统一收录在：

- [InterpreterContext、CodeObject 与 Frame 设计](./runtime_execution_objects_design.md)

其中：

- `InterpreterContext` 保存解释器会话级状态和当前调用链入口。
- `CodeObject` 保存冻结后的优化 IR、slot/value layout、签名和 persistent storage。
- `Frame` 保存一次调用的 slot、binding state、ValueId temporary 和 IR continuation。

workspace 的按名语义仍由 [M 工作区设计](./workspace_design.md) 维护；变量类别和 clear 规则由
[M 变量模型设计](./variable_model_design.md) 维护。
