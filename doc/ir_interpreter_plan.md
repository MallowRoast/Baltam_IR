# Baltam_IR 解释器说明

当前仓库已经有一版启用中的 IR 解释器：

- [src/interpreter/interpreter.h](/home/zj/Desktop/Baltam_IR/src/interpreter/interpreter.h)
- [src/interpreter/interpreter.cpp](/home/zj/Desktop/Baltam_IR/src/interpreter/interpreter.cpp)

它当前面向的是 `UntypedSSA`。CLI 主入口还没有直接调用解释器，但测试已经覆盖：

`parse -> lower(non-SSA) -> verify -> construct_untyped_ssa -> verify -> execute(UntypedSSA)`

## 当前解释器的输入

当前解释器接口是：

```cpp
ExecResult execute_function(Function& function,
                            const std::vector<Value::Object>& args = {},
                            const ExecutionOptions& options = {});
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
using Value::Object = std::shared_ptr<ba_obj>;
```

解释器对 SSA 值再包一层：

```cpp
struct Value {
    enum Type {
        Concrete,
        Undef,
    };

    Type type = Undef;
    Object object;
};
```

返回结果为：

```cpp
struct ExecResult {
    std::vector<Value> outputs;
    std::unordered_map<ValueId, Value> values;
    std::vector<NamedBindingSnapshot> final_named_bindings;
};
```

其中：

- `outputs` 是函数返回值
- `values` 保留执行后所有 SSA 值槽位的最终状态，方便测试和调试
- `final_named_bindings` 保留本次执行路径退出时的最终 `name -> ValueId` 绑定，方便按名字检查

当前 `ExecutionOptions` 还支持：

- 把“函数执行结束后的最终具名绑定”打印到指定输出流
- 透传共享 `RuntimeWorkspace`，承载 `global` 工作区
- 递归透传到模块内函数调用，从而在不修改 SSA IR 的前提下观察每次函数执行结束时的名字状态

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
- `SSAGlobalLoadNode`
- `SSAGlobalStoreNode`
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
- `__ir_cell_get__`
- `__ir_cell_set__`
- `__ir_paren_set__`
- `__ir_getfield_for_write__`

当前已显式静态缓存的 internal function 有：

- `if_expr`
- `switch_case_match`
- `foreach_init`
- `foreach_iterate`

这些 internal function 都是在 IR 生成阶段就已知的固定符号；解释器不再保留 generic internal lookup fallback。

当前对函数句柄的支持范围是：

- `fh_anonymous`
- `fh_mfunction`
- `fh_script`
- `fh_builtin`

`fh_variable` 仍未支持。

## 当前 `global` 语义

解释器执行 `SSAGlobalLoadNode` / `SSAGlobalStoreNode` 时，会读写 `ExecutionOptions.workspace` 里的共享 `RuntimeWorkspace`。

当前语义是：

- missing `global` 读取返回空 `double([])`
- `global.store` 总是写回对象拷贝
- 模块内函数互调会沿用同一个 `workspace`

更完整的设计说明见 [global_design.md](/home/zj/Desktop/Baltam_IR/doc/global_design.md)。

## 圆括号应用语义

当前对圆括号应用 `A(...)` 的处理刻意分成两类：

- `A(...) = B`
- 表达式位置的 `A(...)`

设计边界如下：

- `A(...) = B` 可以在 lowering 阶段直接唯一化成 `__ir_paren_set__`
- 表达式位置的 `A(...)` 不能在 lowering 阶段直接唯一化成 `block get`

原因是：

- 对赋值左值来说，AST 已经给出了明确的赋值语境；`node_asgn(node_multiple_func, rhs)` 不再有“普通函数调用”的歧义
- 对表达式位置来说，`A(...)` 既可能是：
  - 普通函数调用
  - 函数句柄变量调用
  - 矩阵 / 元胞 / 其他运行时对象的圆括号取值

因此当前实现采用：

- lowering 遇到 `A(...) = B` 时，生成 `call @__ir_paren_set__(A, idx..., rhs)`
- lowering 遇到表达式位置或语句位置的 `A(...)` 时，会先按静态名字绑定补一层分类：
  - parser 已明确标成变量，或 lowering 已知这是当前函数里的用户变量名，则生成 `Indirect CallNode`
  - 否则保留成 `Direct CallNode`
- 解释器只在 indirect call 路径上，再根据 callee 的运行时动态类型分派：
  - 若是 `function_handle`，按函数调用执行
  - 若不是 `function_handle`，按 runtime `block get` 执行

这意味着：

- `block_set` 的语义在 lowering 阶段就可以区分
- `block_get` 的语义仍然必须等到运行时才能最终区分
- 但 `A(...)` 是否先走 direct / indirect call，不再完全留到运行时，而是会先利用 AST 和已知用户变量名做一次静态判定

另外，`A(:, ...)` 里的 `node_magic_colon` 目前会在 lowering 阶段直接物化成字符矩阵 `":"`，作为 runtime `block` 识别“整维切片”的哨兵值。

另外，runtime `block set` 本身是原地修改风格；解释器在桥接 `__ir_paren_set__` 时会先复制 base 对象，再调用 runtime `block set`，从而保持 SSA 里“旧版本值不被污染”的语义。

同一类“写回 helper”还有两条当前实现已经依赖的规则：

- `__ir_cell_set__` 也会先复制 base，再执行 runtime `brace_set`
- `setfield` / `__ir_getfield_for_write__` 用于 dot setter 的构造链

另外，如果这些 helper 的第一个实参在执行时还是本地 `Undef` / `MissingInput`，解释器会按 helper 类型补默认 base：

- `__ir_paren_set__` -> 空 `double([])`
- `__ir_cell_set__` -> 空 cell
- `setfield` / `__ir_getfield_for_write__` -> 空 struct

这就是 `L(2:3) = rhs`、`c{2} = rhs`、`s.a.b = rhs` 这类语句可以从未初始化本地名字开始构造值的原因。

## 当前边界和限制

当前解释器仍有这些明确边界：

- 只执行 `UntypedSSA`
- 不支持 `TypedSSA`
- 遇到 `undef` 的具体读取通常会抛错
- 返回 `undef` 也会抛错
- 依赖 verifier 先保证 CFG、`phi`、值定义/使用关系基本合法
- 还没有接进 `main.cpp`

上面这条有一个明确例外：写回 helper 的首实参允许用默认空 base 做补全，见上一节。

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

端到端脚本回归测试目前已经覆盖一批 `.m` 脚本，包括早期基础用例以及后续补上的：

- 短路求值与 setter 组合
- `global` 读写与跨函数共享
- `A(...)` / `A.a` / `A{...}` 的读取和写回
- 多返回值、占位输出、`nargin/nargout`、`end`

这些回归统一覆盖：

- 解析 `.m`
- lower 到 non-SSA
- 构建 untyped SSA
- 打印两阶段 IR
- 执行入口函数并校验输出和最终具名变量绑定

当前 `test/m/test2_1/test2_1.m` 还没有完成适配，也尚未接入对应的 `ctest` 回归项。

## 当前结论

解释器已经不再只是长期规划项，而是当前 `UntypedSSA` 阶段的一部分执行基础设施。

不过它目前仍主要服务于：

- SSA 语义验证
- 端到端测试
- 后续优化 pass 的行为回归检查

而不是面向用户的 CLI 执行入口。
