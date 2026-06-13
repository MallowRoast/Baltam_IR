# MATLAB 运算符对 local / import 的分派观察

本文记录 MATLAB R2022b `9.13.0.2049777` 下，运算符语法是否会命中同名 local
function 或 `import` 引入的 package function。

这里的目标不是完整还原 MATLAB 名字解析实现，而是给 IR lowering / 常量折叠提供当前已经
实测过的边界。

## 1. 测试方法

每个 probe 都让同名函数返回一个明显的 sentinel 值：

- function 文件里的 local function 返回 `100` 段。
- function 文件里的 `import A.*` 返回 `200` 段。
- script 文件里的 local function 返回 `700` 段。
- script 文件里的 `import A.*` 返回 `800` 段。

例如：

```matlab
function [op, direct] = local_plus_probe()
op = 1 + 2;
direct = plus(1, 2);
end

function out = plus(a, b)
out = 99;
end
```

如果 `op` 返回 sentinel，说明运算符语法命中了同名 local/import 函数；如果只有
`direct` 返回 sentinel，说明普通函数调用命中了，但运算符语法没有命中。

## 2. 普通运算符矩阵

| 语法 | 函数名 | function 中 local/import | script 中 local/import |
|---|---|---:|---:|
| `a + b` | `plus` | 命中 | 命中 |
| `a - b` | `minus` | 命中 | 命中 |
| `a .* b` | `times` | 命中 | 命中 |
| `a * b` | `mtimes` | 命中 | 命中 |
| `a ./ b` | `rdivide` | 命中 | 命中 |
| `a / b` | `mrdivide` | 命中 | 命中 |
| `a .\ b` | `ldivide` | 命中 | 命中 |
| `a \ b` | `mldivide` | 命中 | 命中 |
| `a .^ b` | `power` | 命中 | 命中 |
| `a ^ b` | `mpower` | 命中 | 命中 |
| `+a` | `uplus` | 命中 | 命中 |
| `-a` | `uminus` | 命中 | 命中 |
| `~a` | `not` | 命中 | 命中 |
| `a == b` | `eq` | 命中 | 命中 |
| `a ~= b` | `ne` | 命中 | 命中 |
| `a < b` | `lt` | 命中 | 命中 |
| `a <= b` | `le` | 命中 | 命中 |
| `a > b` | `gt` | 命中 | 命中 |
| `a >= b` | `ge` | 命中 | 命中 |
| `a & b` | `and` | 命中 | 命中 |
| `a | b` | `or` | 命中 | 命中 |
| `a && b` | 短路逻辑 | 不命中 | 不命中 |
| `a || b` | 短路逻辑 | 不命中 | 不命中 |
| `a:b` / `a:s:b` | `colon` | 不命中 | 命中 |
| `a.'` | `transpose` | 命中 | 命中 |
| `a'` | `ctranspose` | 命中 | 命中 |
| `[a b]` | `horzcat` | 命中 | 命中 |
| `[a; b]` | `vertcat` | 命中 | 命中 |

## 3. 关键差异

### 3.1 `colon` 在 function 和 script 中不同

function 文件中：

```matlab
function [op, direct] = colon_local_probe()
op = 1:3;
direct = colon(1, 3);
end

function out = colon(a, b)
out = 99;
end
```

实测结果：

```text
op
     1     2     3

direct
    99
```

script 文件中：

```matlab
disp(1:3);

function out = colon(varargin)
out = 722;
end
```

实测结果：

```text
colon2 op
   722
```

也就是说：

- function 文件里的冒号语法不命中 local/import `colon`。
- script 文件里的冒号语法会命中 local/import `colon`。
- 直接写 `colon(...)` 在两类文件中都会按普通名字解析命中 local/import。

### 3.2 `&&` / `||` 不按 local/import 的 `and` / `or` 分派

在 function 和 script 中，`true && false` 与 `false || true` 都保持内建短路逻辑结果：

```text
short_and op
   0

short_or op
   1
```

即使当前作用域里存在 local/import `and` 或 `or`，短路语法也不调用它们。

这说明 `&&` / `||` 应作为控制流 lowering 处理，而不是作为普通 `and` / `or` 调用处理。

## 4. 特殊语法

另测了索引、点访问、赋值和 `end`：

| 语法 | 对应函数 | local/import 观察 | 备注 |
|---|---|---:|---|
| `a(i)` | `subsref` | 不命中 | 直接 `subsref(...)` 会命中 |
| `s.x` | `subsref` | 不命中 | 直接 `subsref(...)` 会命中 |
| `a(i) = v` | `subsasgn` | 不命中 | 直接 `subsasgn(...)` 会命中 |
| `s.x = v` | `subsasgn` | 不命中 | 同上 |
| `a(end)` | `end` | 命中 | local/import `end` 返回越界下标时，`a(end)` 会报 badsubscript |
| `f(...)` | 普通函数调用 | 命中 | 按普通名字解析 |

这里的“不命中”只针对 local/import 普通名字解析。对象类型自己的 `subsref` /
`subsasgn` / `end` overload 是另一条基于 operand 类型的分派路径，不应和 local/import
名字遮蔽混为一谈。

## 5. 对 IR lowering 的含义

1. 大多数运算符语法都不能在 lowering 阶段直接固化为 builtin。它们至少需要保留
   local/import/nested/private 等名字解析参与分派的可能。
2. `&&` / `||` 应 lower 成短路 CFG。它们不应复用普通 `and` / `or` 的 local/import
   名字分派规则。
3. `colon` 必须按 unit 类型区分：
   - function 中的 `a:b` / `a:s:b` 不吃 local/import `colon`。
   - script 中的 `a:b` / `a:s:b` 会吃 local/import `colon`。
4. 索引和点访问语法不应因为存在 local/import `subsref` / `subsasgn` 就改写分派；后续只需要
   为对象类型 overload 留出运行时分派路径。
5. `end` 在索引位置会吃 local/import `end`，这会影响 `a(end)` 的 lowering，不能简单把
   `end` 当成纯内建语法常量。

## 6. 对常量折叠的含义

对 function 文件中的标量常量运算符，若存在同名 local/import 运算符函数，就不能直接把
`1 + 2` 这类表达式折叠成 builtin 结果。

对 script 文件更要保守：

- 普通运算符同样吃 local/import。
- `colon` 在 script 中也吃 local/import。
- script 还涉及 `ScriptVar` 绑定失效和 caller/base 环境绑定，不适合作为第一阶段常量折叠对象。

## 7. 相关文档

- [constant_folding_design.md](./constant_folding_design.md)
- [matlab_import_notes.md](./matlab_import_notes.md)
- MathWorks `Function Precedence Order`: https://www.mathworks.com/help/matlab/matlab_prog/function-precedence-order.html
- MathWorks `Implementing Operators for Your Class`: https://www.mathworks.com/help/matlab/matlab_oop/implementing-operators-for-your-class.html
