# CodeObject 构建期优化设计

本文记录 high-level IR 在真正解释执行前的优化分层。核心结论是：

```text
AST -> high-level IR
    -> 环境无关 IR 优化
    -> CodeObject 构建 / 名字解析 / 依赖记录
    -> 环境感知 IR 优化
    -> IR interpreter
```

这里的“环境”包括当前文件位置、`private` 目录、import、路径、builtin/plugin registry、
class/method 表、workspace 中的变量遮蔽，以及后续 `clear / rehash / path` 变化可能造成的解析结果
失效。

## 1. 为什么不能在 lowering 后直接折叠 `1 + 2`

lowering 阶段只从 AST 构造语义 IR。此时 IR 还没有和一次具体运行环境绑定，因此不能把动态 Matlab
运算直接当作内部 primitive。

例如源码：

```matlab
a = 1 + 2;
```

基础 lowering 可以生成：

```text
%0 = const 1
%1 = const 2
%2 = add %0, %1
store %slot_a, %2
```

只要这条 `add` 的 `dispatch_type` 仍是 `Dynamic`，静态 IR pass 就不能假设它一定等价于 builtin
double `plus`。原因是 Matlab 名字和运算分派会受运行环境影响。具体到纯 double 的 `+` 是否会被
`private/plus.m` 影响，还需要单独用 Matlab 行为测试确认；但设计上不依赖这个假设，只要求：

- 未解析的 `Dynamic` 运算不能常量折叠
- 只有已解析到可信的 builtin/internal primitive，或者带 guard 的稳定目标，才能折叠

这个规则也适用于 `sin(1)`、`foo(1)`、`A(1)` 等其它动态调用或 apply。

## 2. Matlab 函数优先级对优化的影响

函数解析不能只看名字。按 Matlab 文档，函数优先级从高到低包括：

1. 当前 workspace 变量
2. 显式 import 的函数或类
3. nested function
4. 当前文件 local function
5. wildcard import
6. private function
7. object function / method
8. 已加载 Simulink model
9. 当前目录函数
10. path 上的函数

同一目录内还存在文件类型优先级，例如 builtin、MEX、Simulink model、`.mlx`、`.p`、`.m` 等。

这意味着：

- `foo(...)` 不能在 lowering 后马上定为 builtin 或 path function
- `BinaryInst(Add)` 不能在 `dispatch_type = Dynamic` 时直接常量折叠
- 类型事实只能辅助分派，不能替代名字解析
- CodeObject cache 必须记录解析依赖，否则环境变化后可能继续执行旧优化结果

参考：[MathWorks Function Precedence Order](https://www.mathworks.com/help/matlab/matlab_prog/function-precedence-order.html)。

## 3. 两类优化 pass

### 3.1 环境无关 IR 优化

这类 pass 可以在 AST -> IR 后立即运行，也可以作为 CodeObject 构建前的 canonical cleanup。
它们不能依赖当前路径、private 目录或 builtin registry 的解析结果。

允许的优化包括：

- CFG simplification
- unreachable block elimination
- constant deduplication
- value rewrite cleanup
- load forwarding
- frame-local dead store elimination
- `dispatch_type = Internal` 且语义已知的 primitive 常量折叠

需要避免的优化包括：

- 折叠 `dispatch_type = Dynamic` 的 `UnaryInst / BinaryInst / CallInst`
- 把 `foo(...)` 静态改写成 builtin call，除非 lowering 已经有合法解析依据
- 跨越可能观察 workspace/frame 的动态调用做 store 消除

### 3.2 CodeObject 构建期环境感知优化

CodeObject 构建时已经知道当前文件路径和调用环境，可以做第二阶段优化：

1. 解析动态调用和动态运算
2. 记录解析依赖和 invalidation guard
3. 把稳定调用点标注为 `Builtin / Internal / MFunction`
4. 基于已解析目标和类型事实做常量折叠
5. 再运行 cleanup pass 删除折叠后的冗余 load/store/copy

示例 pipeline：

```text
parse_and_lower_mfile_to_ir
  -> canonical cleanup:
       constant-deduplication
       load-forwarding
       cfg-simplification
  -> build CodeObject:
       resolve-call-and-operator
       record-codeobject-dependencies
       env-aware-constant-folding
       dead-store-elimination
       load-forwarding
       cfg-simplification
       verify
  -> publish immutable CodeObject
```

CodeObject 发布后应视为不可变。如果任何依赖变化，应构造新的 revision，而不是原地修改活跃 Frame
正在执行的 IR。

## 4. CodeObject 依赖与失效

环境感知优化必须把“为什么这个优化成立”记录到 CodeObject 上。第一版可以先记录粗粒度 generation，
后续再细化到单个名字。

建议依赖项：

- source file revision
- 当前文件所在目录和 `private` 目录的 lookup generation
- 当前 `CodeObject::private_functions` 快照
- path generation / current folder generation
- import 表 generation
- local / nested function table revision
- builtin / plugin registry generation
- class / method registry generation
- global `clear functions` / `rehash` generation

如果某个调用点从 `Dynamic` 收敛为 builtin `plus`，则 CodeObject 至少需要记录：

- 当前名字解析结果仍指向 builtin `plus`
- 当前参数类型事实仍满足该 builtin fast path
- 相关 path/private/class/builtin generation 未变化

当 guard 失效时，解释器应回退到重新构建 CodeObject 或执行未优化 high-level IR。

## 5. 常量折叠规则

常量折叠的安全条件：

- 所有输入操作数都是编译期常量
- 指令目标已经解析为确定的纯函数或内部 primitive
- 该目标没有 workspace/path/global/persistent 可观察副作用
- 折叠结果能用当前 `Constant` 表达
- 依赖 guard 已记录，或该 primitive 完全环境无关

可以折叠：

```text
%0 = const 1
%1 = const 2
%2 = internal.add %0, %1
```

或 CodeObject 阶段确认后的：

```text
%2 = add %0, %1 ; resolved: builtin double plus, guarded
```

不能折叠：

```text
%2 = add %0, %1 ; Dynamic
%3 = call @foo(%0) ; Dynamic
```

## 6. Dead Store Elimination 边界

`store` 消除可以在两阶段都做，但要区分 slot 是否可见。

第一版只建议处理：

- `Local`
- `Ret`
- `InternalLocal`

先不要处理：

- `ScriptVar`
- `BaseVar`
- `Global`
- `Persistent`
- `Capture`

因为这些 slot 可能被 workspace、global table、persistent table、closure 或动态机制观察。

单 basic block 内的保守规则：

- `store %slot, %v` 后没有 `load %slot` 消费这次写入
- 后续没有 `call/apply/value_apply/magic_end` 等 opaque barrier
- 后续没有 dynamic `UnaryInst / BinaryInst` barrier
- 后续同 slot store 覆盖了这次写入，或 `ReturnInst` 已经直接返回 ValueId 而不依赖 ret slot

对于：

```matlab
function s = test10()
s = 1;
s = s + 1;
s = s + 2;
end
```

当前 `--run-passes` 后大致是：

```text
%0 = const 1
store %slot0, %0
%3 = add %0, %0
store %slot0, %3
%5 = const 2
%6 = add %3, %5
store %slot0, %6
ret %6
```

如果 `add` 仍是 dynamic，则 DSE 不能跨过它随意删除前面的 `store`，因为 dynamic `add` 可能进入用户
代码或其它 opaque 路径。CodeObject 阶段若已证明这些 `add` 是纯 builtin double plus，则可以先常量
折叠，再删除所有不再被读取的 ret slot store，最终接近：

```text
%0 = const 4
ret %0
```

## 7. `add` 是否会隐式 load

当前 high-level IR 的 `BinaryInst(Add)` 操作数是 `Operand`，通常引用 `ValueId`：

```text
%3 = add %0, %0
```

这条指令本身不会从 `%slot` 里隐式 load。slot 读取必须通过显式 `LoadSlotInst` 表达。

但 dynamic `add` 的执行可能触发 Matlab 运行时分派，进而调用用户代码、class method 或其它 opaque
逻辑。因此优化时要把问题分成两层：

- 数据流层：`add %0, %0` 不读取 slot
- 语义层：dynamic `add` 可能有不可见副作用或环境观察能力

因此 DSE / constant folding 可以利用数据流事实，但必须受分派解析结果约束。

## 8. 实现顺序建议

第一阶段：

- 增加 CodeObject 构建入口，把当前 `IRBuildResult` 冻结成 `CodeObject`
- 在 CodeObject 上记录粗粒度 resolver generation
- 把当前默认 pass pipeline 挪成 CodeObject build pipeline 的一部分
- 新增单 basic block 的 frame-local DSE，先只处理 `Local/Ret/InternalLocal`

第二阶段：

- 增加 `resolve-call-and-operator` pass
- 为 `BinaryInst / UnaryInst / CallInst` 记录解析事实
- 对已解析 internal/builtin numeric primitive 做常量折叠
- 常量折叠后重复运行 load forwarding / DSE / CFG cleanup

第三阶段：

- 细化 per-call-site dependency
- 增加 guard 失效后的 CodeObject invalidation / recompile
- 接 profile，把热点 region 提升到 typed SSA
