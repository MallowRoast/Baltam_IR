# MATLAB 运算符常量折叠设计

本文记录当前阶段 MATLAB 运算符常量折叠的实现边界、静态判定规则，以及 `private` 与函数体缓存的关系。

如果把目标压成一句话，那么：

> 只在函数文件中，对可静态确认对象函数分派目标的标量运算符表达式做常量折叠。

## 1. 当前边界

当前设计只做下面三件事：

1. 只做 `bool`、8 种整数、`double`、`complex` 这几类数值标量常量。
2. 只做函数文件中的常量折叠。
3. 只做运算符的常量折叠。

原因分别是：

- 先只做这些数值标量，是因为它们的运算符语义最清晰，最容易先建立“类型可确认 + 结果可求值”的闭环。
- 先只做函数文件，是因为脚本名字语义依赖 workspace，很难稳定地静态确定相关符号到底会被当成变量还是函数。
- 先只做运算符，是因为普通函数调用通常太动态，难以静态确定最终分派；而运算符对这些标量更容易收敛到对应类的对象函数分派。

当前明确不处理：

- 脚本文件中的常量折叠
- 数组 / 矩阵常量折叠
- 一般函数调用的常量折叠
- `eval`、函数句柄、`feval` 等更动态场景

## 2. 静态判定规则

### 2.1 判定顺序

一个运算符表达式只有在下面 5 步都成立时，才允许常量折叠：

1. 操作数都是当前支持的标量常量类型。
2. 操作数的精确类型可静态确认。
3. 高于 object function 的竞争者都已静态排除。
4. 命中的 object function 可静态确认。
5. 该 object function 在这些常量参数上的结果可直接求值。

### 2.2 为什么先看高优先级竞争者

- 运算符一般分派到操作数对应类的 object function。
- object function 的优先级高于当前文件夹和 path 上的普通函数。
- 因此，对常量折叠来说，首要问题不是 path 上有没有普通同名函数，而是更高优先级的规则会不会改写分派结果。

当前最相关、且优先级高于 object function 的竞争者主要是：

- 变量
- 显式 `import`
- nested function
- local function
- wildcard `import`
- private function

### 2.3 当前采用的静态检查

- `变量`：依赖前端名字解析。只要能静态确定这里不是函数名，就不再进入对象函数分派分析。
- `显式 import`：通过 AST 收集 `import A.f` 一类语句。只要它可能引入当前运算符对应的函数名，例如 `plus`，就保守禁用该运算符的折叠。
- `nested function`：检查当前函数内部是否定义了同名 nested function；若存在，则禁用对应运算符折叠。
- `local function`：检查同一文件内是否存在同名 local function；若存在，则禁用对应运算符折叠。
- `wildcard import`：对 `import A.*` 这类批量引入名字的情况，当前阶段直接保守禁用对应运算符折叠。
- `private function`：在缓存函数体时检查该函数所在目录下是否存在 `private/`；若存在，则先保守禁用该函数中的运算符常量折叠。

## 3. `private/` 与函数体缓存

对当前阶段，`private` 不宜只当成一个普通的名字解析细节；更合适的做法，是把它和函数体缓存放在一起考虑。

当前采用的工程结论是：

- 在缓存函数体时，根据该函数所在目录下是否存在 `private/`，来保守判断该函数中的运算符表达式是否允许常量折叠，是合理的。

理由是：

- 若缓存函数体时同目录已经存在 `private/`，那么这里已经存在一组优先级高于 object function 的潜在竞争者；直接禁用最安全。
- 若缓存函数体时不存在 `private/`，那么按当时可见的分派结果做折叠是合理的。
- 后续运行期新增或删除 `private`，更适合通过函数体缓存失效来处理，而不是要求常量折叠单独追踪运行期重绑定。
- 因此 `private/` 不只是静态准入条件，也是函数体缓存与常量折叠缓存的失效条件之一。

换句话说：

- 若函数体首次缓存时目录里不存在 `private/plus.m`，那么 `1 + 2` 可以按当时的数值 `plus` 分派做常量折叠。
- 若运行期后来新增了 `private/plus.m`，那么当前这份已经加载的函数体里，旧的 `1 + 2` 折叠结果仍可继续沿用；这是因为它跟随的是旧函数体缓存，而不是因为新增 `private` 后静态上仍然允许继续放行折叠。
- 一旦函数体缓存失效，那么对应的常量折叠缓存也必须一起失效。
- 但“函数体缓存失效”与“文件可见性刷新”是两件事。单独 `clear functions` 只会让函数体与调用点绑定失效，不会刷新 MATLAB 对新增/删除 `private` 文件的认知。
- 因此，单独 `clear functions` 之后，函数体虽然会重新加载，但仍可能按旧的文件可见性重新建立 `1 + 2` 的分派绑定。
- 从可观测行为上看，`clear functions` 更接近“移除已加载函数体及其调用点绑定”，而不是“从名字可见性或符号层移除某个候选函数”。若它做的是后者，那么运行期新增或删除 `private/plus.m` 之后，单独 `clear functions` 就应当更容易直接收敛到新分派；但 probe 显示它不会，说明名字可见性这一层仍保留旧认知，直到 `rehash` 发生。
- 只有“函数体重新加载”与“当前可见性已刷新”同时成立时，`1 + 2` 才会按当时的 `private` 状态重新分派，再决定是否还能折叠。
- 对这个 probe 而言，这通常对应 `clear functions + rehash` 或 `rehash + clear functions`；前者保证调用点绑定失效，后者保证重建绑定时看到的是新的文件系统状态。

`runtime_private_plus_dispatch_probe` 的具体过程与观测结果见 [runtime_private_plus_dispatch_probe.md](/home/zj/Desktop/Matlab/doc/runtime_private_plus_dispatch_probe.md:1)。

## 4. 后续计划

后续可以继续往这几个方向扩展：

1. 将常量范围推广到字符串字面量和数值矩阵，并支持 `[]` 与 `colon` 的常量折叠。
2. 细化 `import` 的分析。例如：对函数语法的 `import(...)`、`import A.*` 这类情况，保守禁用常量折叠；对命令语法的 `import A.f`，则继续细化，不必一概禁用。
3. 细化对 `private` 的处理。不是只要存在 `private/` 就禁用，而是只有 `private` 中存在当前运算符对应的函数时，才禁用对应的常量折叠。
4. 支持 `isreal`、`is_empty`、`sin` 等 MATLAB 已经注册成对象函数的常量折叠；前提仍然是分派目标可静态确认，且这些常量参数上的结果可直接求值。

## 5. 参考

- MathWorks `Function Precedence Order`: https://www.mathworks.com/help/matlab/matlab_prog/function-precedence-order.html
- MathWorks `Implementing Operators for Your Class`: https://www.mathworks.com/help/matlab/matlab_oop/implementing-operators-for-your-class.html
- MathWorks `Method Invocation`: https://www.mathworks.com/help/matlab/matlab_oop/method-invocation.html
- MathWorks `Class Precedence`: https://www.mathworks.com/help/matlab/matlab_oop/class-precedence.html
- MathWorks `Numeric Types`: https://www.mathworks.com/help/matlab/numeric-types.html

## 附录

`runtime_private_plus_dispatch_probe` 的完整观察记录、现象矩阵，以及 `clear plus` / `clear functions` / `rehash` 的差异，见 [runtime_private_plus_dispatch_probe.md](/home/zj/Desktop/Matlab/doc/runtime_private_plus_dispatch_probe.md:1)。
