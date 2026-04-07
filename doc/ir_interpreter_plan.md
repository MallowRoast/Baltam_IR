# Baltam_IR 解释器说明

## 当前状态

当前仓库已经有一版启用中的 IR 解释器：

- [src/interpreter/interpreter.h](/home/zj/Desktop/Baltam_IR/src/interpreter/interpreter.h)
- [src/interpreter/interpreter.cpp](/home/zj/Desktop/Baltam_IR/src/interpreter/interpreter.cpp)

它当前面向的不是旧 hybrid/value-based IR，而是：

- `UntypedSSA`

当前 CLI 主入口还没有直接调用解释器，但测试里已经覆盖：

`parse -> lower(non-SSA) -> verify -> construct_untyped_ssa -> verify -> execute(UntypedSSA)`

## 当前解释器的输入

当前解释器接口是：

```cpp
ExecResult execute_function(Function& function, const std::vector<RuntimeObject>& args = {});
```

要求：

- `function.stage() == IRNode::UntypedSSA`
- 函数已通过 verifier
- `argument_values()` 与输入签名一致
- 实参数量与参数槽位个数一致

也就是说，当前解释器执行的是：

- SSA 形式的 `Function`
- SSA 形式的 `BasicBlock`
- SSA 节点

而不是 `NonSSANode`。

## 当前运行时值

当前运行时仍然复用 Baltam 运行时对象：

```cpp
using RuntimeObject = std::shared_ptr<ba_obj>;
```

解释器对 SSA 值再包一层：

```cpp
struct RuntimeValue {
    enum Type {
        Concrete,
        Undef,
    };

    Type type;
    RuntimeObject object;
};
```

返回结果为：

```cpp
struct ExecResult {
    std::vector<RuntimeValue> outputs;
    std::unordered_map<ValueId, RuntimeValue> values;
};
```

其中：

- `outputs` 是函数返回值
- `values` 保留执行后所有 SSA 值槽位的最终状态，方便测试和调试

## 当前执行模型

当前最小执行模型已经落地：

- 进入函数时，把实参写入 `argument_values()`
- 逐块执行 `phi`
- 再执行 `instructions()`
- 最后执行 `terminal`
- 块间跳转通过 `predecessor` 选择 `phi incoming`

当前已支持的 SSA 节点包括：

- `SSANumberNode`
- `SSATextNode`
- `SSAUndefNode`
- `SSAPhiNode`
- `SSACopyNode`
- `SSAUnaryOpNode`
- `SSABinOpNode`
- `SSACallNode`
- `SSACondJumpNode`
- `SSAJumpNode`
- `SSAReturnNode`

## 当前调用语义

当前解释器已经支持：

- 直接调用模块内函数
- 直接调用 builtin
- 直接调用 internal function
- 间接调用函数句柄

当前已内建处理的 helper 有：

- `__ir_make_cell__`
- `__ir_make_function_handle__`
- `__ir_switch_match__`

当前对函数句柄的支持范围是：

- `fh_anonymous`
- `fh_mfunction`
- `fh_script`
- `fh_builtin`

`fh_variable` 仍未支持。

## 当前边界和限制

当前解释器仍有这些明确边界：

- 只执行 `UntypedSSA`
- 不支持 `TypedSSA`
- 遇到 `undef` 的实用读取会抛错
- 返回 `undef` 也会抛错
- 依赖 verifier 先保证 CFG、`phi`、值定义/使用关系基本合法
- 还没有接进 `main.cpp`

另外，解释器并不替代 SSA 构建器；它默认输入已经是：

- [src/optimizer/construct_untyped_ssa.cpp](/home/zj/Desktop/Baltam_IR/src/optimizer/construct_untyped_ssa.cpp)

产出的结果。

## 当前测试覆盖

当前直接覆盖解释器语义的测试位于：

- [test/interpreter_test.cpp](/home/zj/Desktop/Baltam_IR/test/interpreter_test.cpp)

它目前覆盖：

- 常量和 copy
- 分支与 `phi`
- `undef` 返回报错
- 直接模块函数调用
- 间接函数句柄调用

端到端执行样例位于：

- [test/simple_demo_test.cpp](/home/zj/Desktop/Baltam_IR/test/simple_demo_test.cpp)

它覆盖：

- 解析 `.m`
- lower 到 non-SSA
- 构建 untyped SSA
- 执行入口函数并校验输出

## 当前结论

解释器已经不再只是长期规划项，而是当前 `UntypedSSA` 阶段的一部分执行基础设施。

不过它目前仍主要服务于：

- SSA 语义验证
- 端到端测试
- 后续优化 pass 的行为回归检查

而不是面向用户的 CLI 执行入口。
