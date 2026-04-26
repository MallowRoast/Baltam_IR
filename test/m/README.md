# M Test Cases

这个目录存放用于 IR parse/lowering/print smoke test 的 `.m` 样例。

这些样例不是为了覆盖完整 Matlab 语义，而是为了稳定验证当前 IR lowering
最容易出错的那一层：同样的源码形状，在不同工作区语义下，会不会被 lowering 成不同的 IR。

这里最重要的区别是：

- `script` 中的普通名字访问依赖当前 workspace，很多名字在 lowering 时不能过早静态定性
- `function` 中的参数、返回值和局部变量具有更强的静态约束，名字一旦被当作变量绑定，后续语义就应该稳定下来

因此，这两个测试文件故意保持源码骨架接近，只改变“脚本 vs 函数”这一件事。这样 smoke test
可以更清楚地暴露 lowering 的语义差异，而不是被无关语法噪声掩盖。

## test0

文件：`test/m/test0/test0.m`

测试侧重点：

- 脚本场景下，普通名字访问会 lowering 成 `LoadWorkspaceInst` / `StoreWorkspaceInst`
  打印时分别显示为 `load_env` / `store_env`
- `sin(a)` 在脚本中保留源码层圆括号应用歧义，因此 `sin` 只会 lowering 成 `apply` 节点
- 继续覆盖基本的算术、`if/else`、源码行号注释和 script 环境槽位打印

为什么需要这个用例：

- 脚本里的名字不是局部 frame 上的静态变量名，而是“当前 workspace 中的名字”。
  如果这里误 lower 成 `load_slot` / `store_slot`，就等于把脚本错误地当成了函数体处理。
- `sin(a)` 在脚本里不能因为名字长得像函数就直接定成 `call`，因为脚本工作区里的 `sin`
  运行时可能被同名变量覆盖。当前 IR 需要保留这种歧义，所以应该生成 `apply`。
- 这个用例因此是“动态名字访问”路径的基线样例，用来防止脚本 lowering 被过早静态化。

## test0_1

文件：`test/m/test0_1/test0_1.m`

测试侧重点：

- 函数场景下，参数、返回值和局部变量访问会 lowering 成 `load_slot` / `store_slot`
- `sin(a)` 在函数中按函数名分派，因此 `sin` 会 lowering 成 `call` 节点
- 继续覆盖返回槽位、`if/else` 和隐式 `ret`

为什么需要这个用例：

- 函数与脚本的关键差异之一是：函数内的变量语义应该在 lowering 期稳定下来。
  参数、返回值和已赋值的局部名字都应进入 lowering 期名字绑定表，并映射到 slot。
- 在这种前提下，如果 `sin` 没有先被绑定成变量，那么 `sin(a)` 就不应再保留为源码层歧义，
  而是应该直接 lower 成 `call`。这体现的是函数工作区里更强的静态分派能力。
- 这个用例用来保证函数 lowering 不会错误地沿用脚本那套“全部先保留为 apply”的策略，
  否则函数里的调用分派就会过于保守。

## 设计原则

这两个样例刻意保持很小，并且复用相同的算术和分支骨架，原因是：

- 便于直接对比 script / function lowering 的差异
- 便于 printer 的输出做稳定断言
- 一旦测试失败，更容易判断是“名字解析策略变了”，还是“控制流/算术 lowering 坏了”
