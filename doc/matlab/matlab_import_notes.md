# MATLAB `import` 机制笔记

这份笔记整理了 MATLAB 解释器视角下 `import` 的机制、作用域、执行时机，以及它对名字解析和函数分派的影响。

`import` 对运算符语法的影响有一份独立实测记录，见
[operator_local_import_dispatch_probe.md](./operator_local_import_dispatch_probe.md)。这里最容易踩坑的是
`colon`：function 文件中的 `1:3` 不吃 `import A.colon`，但 script 文件中的 `1:3`
会吃 `import A.colon`。

## 1. `import` 是什么，以及推荐写法

`import` 的核心作用，不是“加载一个模块对象”，而是：

- 将 namespace 中的类、函数或静态方法加入当前作用域的 `import list`
- 让后续代码可以只写短名字，而不用每次都写完整限定名
- 改变当前作用域里的名字解析结果

因此更准确地说，`import` 更像一种当前作用域的名字绑定机制，而不是普通的顺序执行语句。

按当前 MathWorks 官方 `import` 文档，正式列出的语法是：

```matlab
import pkg.ClassName
import pkg.funcName
import pkg.ClassName.staticMethod
import pkg.*
import
L = import
```

这说明至少在官方文档层面：

- `import` 的主用法是命令语法
- MATLAB 推荐把导入目标直接写在源码里
- `import` 更像当前作用域里的名字绑定声明，而不是普通的动态 API

### 1.1 命令语法

```matlab
import pkg.ClassName
import pkg.funcName
import pkg.*
```

特点：

- 导入目标直接写在源码里
- 不依赖运行时变量求值
- 对命令语法，MATLAB 会先预处理 `import`

### 1.2 非推荐的函数调用式写法

```matlab
name = "pkg.funcName";
import(name)
```

或：

```matlab
ns = "pkg";
import(ns + ".*")
```

这类写法在实践中是可用的，但需要特别说明：

- `import(expr)` 不在当前官方 `import` 文档列出的语法里
- 因此不应把它视为 MATLAB 推荐用法
- 从兼容性角度，更稳妥的假设是：后续版本可能调整甚至移除这种写法

如果只是讨论解释器在接受这种写法时更接近什么语义，它的特点是：

- 参数先按普通函数调用规则求值
- 导入目标可能依赖运行时值
- 它首先仍是一条普通运行期调用

因此后文提到“函数语法 `import(...)`”时，只是在分析这种非推荐写法一旦被接受时的行为，不代表它是官方建议依赖的接口。

### 1.3 导入类

```matlab
import pkg.ClassName
obj = ClassName(...)
```

用途：

- 后续可以直接写 `ClassName(...)`
- 不必每次都写 `pkg.ClassName(...)`

### 1.4 导入 namespace 函数

```matlab
import pkg.funcName
y = funcName(x)
```

用途：

- 把 `pkg.funcName(...)` 缩短成 `funcName(...)`

### 1.5 导入静态方法

```matlab
import pkg.ClassName.staticMethod
v = staticMethod(x)
```

用途：

- 调用静态方法时只写最后一段名字

### 1.6 通配导入

```matlab
import pkg.*
```

用途：

- 把一个 namespace 下的成员批量加入当前 `import list`

但官方明确不建议把它当默认写法，因为：

- 会把一组不透明的名字引入当前作用域
- 更容易和局部变量、local function、path 上的函数发生冲突

### 1.7 查看当前作用域的 `import list`

```matlab
import
L = import
```

用途：

- `import`：显示当前作用域的导入列表
- `L = import`：把当前导入列表取出来

## 2. 作用域

MATLAB 官方把 `import` 的作用域分成 3 类。

### 2.1 函数作用域

包括：

- 普通函数
- nested function
- local function

核心规则：

- `import` 的作用域是整个函数
- 这包括文本上写在 `import` 之前的代码
- 函数的 `import list` 会跨多次调用保持
- 只有在函数被 `clear` 后，这个函数的 `import list` 才会被清掉

补充理解：

- nested function 会继承 parent function 的 `import`
- 但独立的 function body 仍应看成各自维护自己的函数级 `import list`

例如：

```matlab
function out = f()
    out = fromName("double");
    import matlab.metadata.Class.fromName
end
```

这里更准确的理解不是“运行到第二行才开始生效”，而是：

- MATLAB 在处理这个函数时，会先把 `import` 纳入该函数的名字解析环境
- 所以前面那一行也能看到这个导入

### 2.2 脚本作用域

核心规则：

- `import` 的作用域是整个脚本体
- 同样包括文本上写在 `import` 之前的代码
- 但脚本里的导入只在这个脚本体内部可见
- 它不会泄漏到调用它的外层作用域

这意味着：

- 脚本的 `import` 不是修改 base workspace 的全局导入表
- 它只对该脚本本体生效

所以：

- 脚本里能直接使用的短名字，不会自动提供给它调用的函数
- 如果某个函数也想使用短名字，它仍然需要自己声明 `import`

### 2.3 命令行 / base workspace 作用域

核心规则：

- 在 MATLAB Command Window 直接输入的 `import`
- 作用域是 base workspace 中随后在命令行执行的代码

例如：

```matlab
import java.lang.String
s = String("hello")
```

这类导入只对命令行上下文生效。

清空方式也和函数 / 脚本不同：

```matlab
clear import
```

这是清 base workspace 的导入列表。官方同时明确说明：

- 不要在函数内部或脚本内部调用 `clear import`

## 3. 执行时机

MATLAB 会在同一文件的其他语句之前，先预处理命令语法 `import` 语句：

因此：

- 命令语法 `import` 对名字解析的影响，不应该按“运行到这一行才开始生效”来理解
- 对函数和脚本中的命令语法 `import`，更接近“进入该作用域时，导入列表已经建立好”

这也解释了两个行为：

1. `import` 可以影响文本上位于它前面的代码
2. 不应把命令语法 `import` 放进函数内的条件语句里指望它只在某个分支生效

例如：

```matlab
function f(flag)
    if flag
        import pkg.funcName
    end
    funcName()
end
```

这里更接近的语义不是“有时执行 import，有时不执行 import”，而是：

- MATLAB 会先预处理 `import`
- 再去处理条件语句里的变量求值和后续名字解析

但这条结论不要直接外推到上面提到的非推荐写法 `import(expr)`。

如果只是讨论解释器在接受这种写法时的行为，更合理的理解是：

- 先求值 `expr`
- 再按其结果执行一次动态 `import`

## 4. `import` 对名字解析的影响

`import` 不只是缩短书写，它还会改变同名函数的解析结果。

MATLAB 当前文档里的函数优先级中，和 `import` 直接相关的关键点是：

1. 变量优先级最高
2. 显式导入的名字（非 wildcard）优先级非常高
3. nested function
4. local function
5. wildcard 导入匹配到的名字

这意味着至少要区分两种情况：

- 显式导入：`import pkg.funcName`
- wildcard 导入：`import pkg.*`

它们的优先级不是一回事。

尤其值得注意的是：

- 显式导入的同名函数，优先级高于一般外部函数查找
- wildcard 导入的优先级低于 nested / local function

放到函数调用上看，更准确的结论是：

- `import` 改变当前作用域里名字解析的查询顺序
- 它不是把某个短名直接静态绑定到完整限定名

没有 `import` 时，短名调用：

```matlab
a(x)
```

更接近的理解是按 precedence 依次查找，例如先看：

- 变量
- nested / local function
- path 上的普通函数
- builtin

而一旦有：

```matlab
import A.a
```

那么对短名 `a(...)` 的查询顺序就会被改写，`A.a` 会作为高优先级导入项参与查找。

显式导入和 wildcard 导入的区别，本质上也是插入查询顺序的位置不同。例如：

- `import pkg.funcName`
- `import pkg.*`

- 显式导入会把该短名放到很靠前的位置
- wildcard 导入虽然也会参与查找，但位置低于 nested / local function

因此从解释器行为看，至少要区分：

- 它是显式导入还是 wildcard 导入
- 它落在哪种作用域里

原因至少有三类：

1. 变量优先于函数，短名在运行期可能先命中变量
2. 若使用 `import(...)` 这种非推荐写法，它本身要先在运行期解析自己调到谁
3. 即使在函数作用域里，后续代码也可能修改路径，从而影响导入命名空间中的函数是否仍然存在

所以更准确的说法是：

- `import A.a` 改变当前作用域运行期的 `import list`
- 后续 `a(x)` 再依据这份运行期查询顺序去参与分派

## 5. 自定义 `import` 与 caller 的 `import list`

这里最关键的规则可以直接压缩成一句：

- 自定义的同名 `import` 不会影响 caller 当前作用域里的 `import list`

更具体地说：

- `import list` 只有在 builtin `import` 真正在某个作用域里执行时才会修改
- 若 `import(...)` 解析到的是用户自定义 `import`，那它只是一次普通函数调用
- 即使这个自定义 `import` 内部又调用了 builtin `import`，被修改的也只是这个自定义函数自己的当前作用域，而不是 caller 的 `import list`
- 后面删除、改名或新增某个同名 `import.m`，影响的也只是未来某次 `import(...)` 可能解析到谁，不会自动回写或回滚 caller 已有的 `import list`

## 6. 结论

如果只压缩成几条最关键的话，可以记成：

1. MATLAB 的 `import` 本质上是在当前作用域建立 `import list`，用于缩短限定名并改变名字解析。
2. 按当前官方文档，推荐用法是命令语法 `import ...`；`import(expr)` 不属于官方列出的 `import` 语法，不应视为推荐用法，后续存在被调整或移除的风险。
3. `import` 的作用域分为函数、脚本、命令行 3 类。
4. `import list` 更准确地属于当前作用域的运行期名字解析环境；后续短名调用更像按既定优先级顺序依次查找，而不是先构造一个完整候选集合再静态绑定。
5. 在函数里，即使是命令语法 `import A.a`，更准确的理解也应是“先影响函数的 `import list`，再在运行期影响短名调用分派”。
6. 在脚本里，这一点只会更动态，因为脚本运行在外部 workspace 中，变量 / 函数竞争更难提前排除。
7. 若讨论 `import(...)` 这种非推荐写法，只有在它最终真的命中 builtin `import` 并执行时，才会修改当前作用域的 `import list`。
8. 显式导入和 wildcard 导入的优先级不同，解释器行为上必须区分。
9. 即使在函数作用域里，后续代码也可能修改路径，从而影响导入命名空间中的函数是否仍然存在；因此短名函数分派本身仍应保留为运行期问题。
10. 自定义同名 `import.m` 不会直接修改 caller 当前作用域里的 `import list`。

## 参考

- MathWorks `import` 官方文档: https://www.mathworks.com/help/matlab/ref/import.html
- MathWorks `Choose Command Syntax or Function Syntax`: https://www.mathworks.com/help/matlab/matlab_prog/command-vs-function-syntax.html
- MathWorks `Function Precedence Order`: https://www.mathworks.com/help/matlab/matlab_prog/function-precedence-order.html
- MathWorks `Base and Function Workspaces`: https://www.mathworks.com/help/matlab/matlab_prog/base-and-function-workspaces.html
- MathWorks `clear`: https://www.mathworks.com/help/matlab/ref/clear.html
- MathWorks `which`: https://www.mathworks.com/help/matlab/ref/which.html
- MathWorks `Use import in MATLAB Functions`: https://www.mathworks.com/help/matlab/matlab_external/use-import-in-matlab-functions.html
- MathWorks `Import Namespace Members into Functions`: https://www.mathworks.com/help/matlab/matlab_oop/importing-classes.html
