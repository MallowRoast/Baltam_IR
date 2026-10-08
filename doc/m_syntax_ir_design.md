# M 语法子集的 IR 节点设计：讨论记录

本文汇总围绕《M 语法子集（建议）》已经讨论过的表达式 IR 设计，供后续逐项复盘。
它记录的是目标设计，不代表当前 `src/ir` 已实现。下文用“已确定”表示用户明确选定的
方向，用“建议”表示讨论中提出、仍可调整的节点形状。

## 1. 共同原则

- high-level IR 应保留会影响语义的源码构造类别。执行时会调用某个函数，并不意味着
  基础 IR 必须将该源码构造改写为普通 `CallInst`。
- 动态运算在目标尚未解析前不能直接当作内置操作，也不能仅因操作数为常量而折叠。
- `CallInst` 用于显式函数调用或已确认的调用语义；运算符、方括号并置等源码构造可以
  保留专用节点，并在执行或后续 lowering 时映射到相应函数。
- `SourceSpan` 记录位置；需要参与名字解析、分派或索引的源码区别还应以 IR 类型或
  字段明确表示，不能只依赖源码文本。

当前代码中，`+` 已使用 `BinaryInst`；方括号并置和范围冒号仍分别 lower 为
`horzcat` / `vertcat` 和 `colon` 的 `CallInst`。下述设计尚未修改这些实现。

## 2. 常量：`ConstInst`（已确定）

`ConstInst` 后续直接持有 `const ba_obj`，取代目前的 `Constant` variant：

```cpp
class ConstInst final : public Instruction {
public:
    ValueId result = InvalidValueId;
    const ba_obj value;
};
```

构造指令时初始化 `value`，此后不修改 IR 中保存的对象。执行时复制一个 `ba_obj`
包装对象作为运行时值，不把 IR 内的对象直接交给可写的 frame；普通数据由
`ba_obj` 的写时复制机制隔离。现有先默认构造 `ConstInst`、再赋值 `value` 的代码
需要随实现一起调整。

该节点可表示数值标量、字符向量、字符串标量、空矩阵和已确定的矩阵常量。
字符向量与字符串标量必须构造成不同的运行时类型。空 `[]` 是空 double 矩阵常量；
空 `{}` 是空元胞数组常量。

`[1, x]` 依赖运行时值，不能在基础 lowering 中成为常量。即使 `[1, 2]` 的元素都是
常量，也应先确认并置的实际分派目标；同名 local/import `horzcat` 可能改变结果。
在确认目标纯净、稳定且结果可保存后，常量折叠可以产生矩阵 `ba_obj`。
常量折叠可以为已知数值操作提供直接计算路径，不要求一律调用 runtime builtin。

后续生成 typed SSA / LLVM IR 时，可以按 `ba_obj` 的类型和内容选用数值立即数、
静态数据或由 CodeObject 持有的常量池；不要求 LLVM IR 直接表示 C++ 的 `ba_obj`。

## 3. 方括号并置：`ConcatInst`（建议）

用户倾向为源码运算设计专用 IR 节点。建议让非空方括号并置使用 `ConcatInst`，
与 `+` 使用 `BinaryInst` 的方向一致：

```text
ConcatInst
  result       : ValueId
  axis         : Horizontal | Vertical
  items        : 有序参数列表
  dispatch     : 初始为 Dynamic，可附带后续解析结果
```

示例：

```text
[]             -> ConstInst(空 double 矩阵)
[a, b]         -> ConcatInst(Horizontal, a, b)
[a; b]         -> ConcatInst(Vertical, a, b)
[a b; c d]     -> 两个横向并置结果，再做纵向并置
```

横向、纵向并置执行时分别对应 `horzcat`、`vertcat`，可能命中用户定义的实现。
`ConcatInst` 因此不能在基础 lowering 中无条件标为 builtin。若后续解析到 M 函数，
建议在节点上保存解析结果，同时保留“来源于方括号并置”的信息。

`items` 的最终类型要能支持 `C{:}` 这样的逗号分隔列表展开；单纯的
`vector<ValueId>` 不足以完整表示这类参数。具体列表表示留待相关节点讨论。

## 4. 花括号构造：`CellConstructInst`（建议）

`{a, b; c, d}` 使用列分隔和行分隔语法，结果是 2×2 元胞数组，每个输入值放入
对应的元胞。它不是对输入值调用 `horzcat` / `vertcat`：例如 `{C1, C2}` 是
两个分别装着 `C1`、`C2` 的元胞，而 `[C1, C2]` 是拼接两个数组。

建议使用 `CellConstructInst` 保存行边界和每行的有序元素；执行时可由内部元胞分配、
填充操作实现。`{}` 可直接用持有空元胞数组的 `ConstInst`。

`{C{:}}` 会把逗号分隔列表中的各值放入不同元胞，因此元素个数可能在运行时才确定。
该节点的元素表示需要与第 3 节的参数展开设计协调。`C{i}` 属于元胞内容索引，
与花括号构造分开设计。

## 5. 冒号：`ColonInst` 与全维索引标记（建议）

生成序列的 `a:b`、`a:s:b` 建议使用 `ColonInst`：

```text
ColonInst
  result       : ValueId
  start        : Operand
  step         : 省略 | Operand
  stop         : Operand
  dispatch     : 初始为 Dynamic，可附带后续解析结果
```

`step` 必须区分“省略”和“显式写出 1”，保留两参数、三参数运算形式。
范围冒号仍可按类型和作用域动态分派到 `colon`。仓库的 MATLAB 实测显示，
冒号运算符语法与显式 `colon(...)` 调用在部分作用域中的名字解析不同，
因此二者不宜在基础 IR 中无区别地表示。`for i = 1:10` 先得到冒号运算结果，
循环协议再使用该结果。

索引中单独的 `:`（如 `A(:, 2)`、`A(:)`）表示“该索引位置的全部下标”，
不独立生成序列值。建议在索引参数表示中使用 `All` 标记，而不生成 `ColonInst`：

```text
A(:, 2)       -> IndexArgument(All), IndexArgument(Value(2))
A(1:end, 2)   -> IndexArgument(Value(ColonInst(1, MagicEndInst))), Value(2)
```

全维标记还需随尚未消歧的圆括号应用一起保留，供后续确定函数调用还是索引。

## 6. 一元运算：`UnaryInst`（已确定）

一元正号 `+x`、一元负号 `-x` 和逻辑非 `~x` 沿用现有 `UnaryInst`，不新增节点。
`op` 分别取 `Uplus`、`Uminus`、`LogicalNot`；节点继续保存单个操作数、单个结果和
`dispatch_type`。基础 lowering 不把动态分派直接固定为内置实现。

两种转置使用同一个 `UnaryInst` 的决定见第 11 节。

## 7. 二元算术运算：`BinaryInst`（已确定）

`a + b`、`a - b`、`a * b`、`a / b`、`a \ b`、`a ^ b` 沿用现有
`BinaryInst`，`op` 分别为 `Add`、`Sub`、`Mul`、`Rdiv`、`Ldiv`、`Pow`。
节点保存左右操作数、一个结果和分派状态；不为每个运算符另设指令类型，
也不在基础 lowering 中将动态运算固定为普通函数调用或内置实现。

为了保留源码运算符信息，后续即使解析到用户定义的 M 函数，也应在
`BinaryInst` 上记录解析目标，而不是把节点改写为普通 `CallInst`。
这与目前源码中“M 函数目标改写为 `CallInst`”的约定不同，属于后续实现调整。

本节仅确认上述六种算术运算。其他二元运算在对应语法项讨论。

## 8. 逐元素运算：`BinaryInst`（已确定）

`a .* b`、`a ./ b`、`a .\ b`、`a .^ b` 继续使用 `BinaryInst`，
`op` 分别为 `ElemMul`、`ElemRdiv`、`ElemLdiv`、`ElemPow`。
不同 `op` 保留了逐元素运算与矩阵运算的区别；动态分派及解析到用户函数后
保留原节点的规则与第 7 节一致。

## 9. 比较运算：`BinaryInst`（已确定）

`a < b`、`a <= b`、`a > b`、`a >= b`、`a == b`、`a ~= b` 沿用现有
`BinaryInst`，`op` 分别为 `Lt`、`Le`、`Gt`、`Ge`、`Eq`、`Ne`。
不新增 `CompareInst`。动态分派时不预先断定结果为逻辑值；只有解析到语义
明确的实现后，类型分析才能给出相应的结果类型事实。解析目标的保留规则
与第 7 节一致。

## 10. 逻辑运算：`BinaryInst` 与 CFG（已确定）

`a & b`、`a | b` 沿用现有 `BinaryInst`，`op` 分别为 `And`、`Or`，
左右两侧都求值；动态分派及解析目标的规则与第 7 节一致。

`a && b`、`a || b` 需要短路求值，继续由现有的基本块和 `BranchInst` 等
控制流指令表示：先求左侧，再决定是否进入右侧块，最后汇合结果。
当前 lowering 会在两条路径上写入一个 `InternalLocal` 临时 slot，并在汇合块读取它，
以表示整个短路表达式的结果。此 slot 是否可以消除或转成 SSA 值，需要结合控制流、
异常边界及后续优化阶段仔细分析；目前不预设它一定可消除。
目前不为这两种语法设计独立 IR 节点；如果后续出现明确需求，再重新讨论。

## 11. 转置：`UnaryInst`（已确定）

共轭转置 `A'` 和非共轭转置 `A.'` 沿用现有 `UnaryInst`，`op` 分别为
`Ctranspose`、`Transpose`。节点保留操作数、结果及动态分派状态；
解析到用户定义函数后保留源码运算符信息的方向与第 7 节一致。

## 12. 后续待定

- `ConcatInst`、`CellConstructInst` 和调用节点共同使用的逗号分隔列表表示。
- 专用运算节点的解析目标使用何种具体字段；`ConcatInst`、`ColonInst` 是否沿用
  二元算术运算“解析后保留原节点”的规则。
- 空以外的纯字面量矩阵何时可以在基础构建阶段直接成为 `ConstInst`，何时必须等待分派解析。
- `&&` / `||` 的结果临时 slot 能否在特定控制流条件下消除或 SSA 化。
- 专用节点的精确 C++ 字段、effect、verifier 规则及运行时实现。

## 参考

- [当前 IR Schema](./ir_schema.md)
- [运算符 local/import 分派实测](./matlab/operator_local_import_dispatch_probe.md)
- [MATLAB 运算符与对应函数](https://www.mathworks.com/help/matlab/matlab_oop/implementing-operators-for-your-class.html)
- [MATLAB 元胞数组构造](https://www.mathworks.com/help/matlab/matlab_prog/create-a-cell-array.html)
- [MATLAB 冒号运算与索引](https://www.mathworks.com/help/matlab/ref/double.colon.html)
