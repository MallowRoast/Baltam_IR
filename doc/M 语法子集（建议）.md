# M 语法子集（建议）

本文定义自制 MATLAB 解释器第一阶段拟支持的 M 语法范围。目标是覆盖常见数值计算、脚本与函数、动态调用、嵌套函数，以及基础面向对象语法，同时将完整 MATLAB 语言中较复杂的高级特性留待以后扩展。



## **1\. 表达式**



### **1\.1 基本表达式**



- 字面量：数值、字符向量、字符串标量

- 矩阵、行向量与空数组：`[1 2; 3 4]`、`[]`

- 元胞数组：`{a, b; c, d}`

- 冒号表达式：`start:stop`、`start:step:stop`

- 索引中的冒号：`A(:)`、`A(1:end-1)`



### **1\.2 运算表达式**



- 一元运算：正号、负号、逻辑非

- 算术运算：`+`、`-`、`*`、`/`、`\\`、`^`

- 逐元素运算：`.*`、`./`、`.\\`、`.^`

- 比较运算：`<`、`<=`、`>`、`>=`、`==`、`~=`

- 逻辑运算：`&`、`|`、`&&`、`||`

- 转置：共轭转置 `A'`、非共轭转置 `A.'`



### **1\.3 索引表达式**



- 圆括号索引或函数调用：`A(i)`、`f(x)`、`obj.method(x)`；支持多维索引，例如 `A(:, 2)`、`A(1, 2, 3)`

- 元胞索引：`C(i)` 返回元胞数组，`C{i}` 返回元胞内容；两种形式均可用于赋值，例如 `C(i) = {x}`、`C{i} = x`

- 字段或属性访问：`s.field`、`obj.Property`

- 命名空间限定名：`pkg.func(x)`、`pkg.ClassName`；名称解析需区分命名空间成员与变量的字段或属性

- 动态字段访问：`s.(name)`、`obj.(name)`

- 后缀可连续组合，例如 `obj.Items{i}.Value`

- 逗号分隔列表：`C{:}`、`S.field`（结构体数组），可在函数实参、数组构造和多输出赋值等位置展开，例如 `f(varargin{:})`、`[a, b] = C{:}`

`f(x)` 可能表示函数调用，也可能表示数组索引。语法树可先统一表示为后缀应用节点，再由运行时环境确定具体含义。



### **1\.4 函数句柄与匿名函数**



- 命名函数句柄：`@sin`、`@myFunction`

- 嵌套函数的函数句柄：在外层函数内使用 `@inner` 获取嵌套函数 `inner` 的句柄，并可将该句柄作为输出返回

- 匿名函数：`@(x) x.^2`、`@(x, y) x + y`

- 函数句柄调用：`h(x)`

- 匿名函数和嵌套函数句柄可保留其定义位置所需的外层变量；捕获、共享与生命周期规则由运行时语义定义

## **2\. 语句**



### **2\.1 普通语句**



- 赋值：`x = expr`

- 多输出赋值：`[a, b] = f(x)`；允许用 `~` 忽略不需要的输出，例如 `[~, index] = max(A)`

- 索引、字段与属性作为赋值目标：`A(i) = x`、`C(i) = {x}`、`C{i} = x`、`s.field = x`、`obj.Property = x`

- 用空数组删除数组或元胞元素：`A(2) = []`、`C(2) = []`

- 表达式语句：例如 `disp(x)`

- 导入语句：`import pkg.ClassName`、`import pkg.func`、`import pkg.*`

- 语句结束与输出抑制（由分号等语句结束形式表达）

### **2\.2 控制流**



- 条件语句：`if`、`elseif`、`else`、`end`

- 分支语句：`switch`、`case`、`otherwise`、`end`

- 条件循环：`while`、`end`

- 迭代循环：`for name = expr`、`end`

- 循环控制：`break`、`continue`

- 函数返回：`return`

### **2\.3 变量存储声明**



- 全局变量声明：`global x y`

- 持久变量声明：`persistent cache state`

`global` 与 `persistent` 均需纳入语法。变量共享范围、初始化和生命周期属于运行时语义：全局变量跨函数共享；持久变量在函数调用结束后保留，并在后续调用中继续使用。



### **2\.4 错误处理**



- `try`、`catch`、`end`

- 支持无捕获变量的 `catch`，以及带异常变量的 `catch ME`

## **3\. 函数定义与调用**



### **3\.1 函数定义**



支持零个或多个输入和输出参数：



```MATLAB
function y = square(x)
    y = x.^2;
end
```



```MATLAB
function [quotient, remainder] = divide(a, b)
    quotient = floor(a / b);
    remainder = rem(a, b);
end
```



函数定义需要涵盖：



- 无输出、单输出和多输出函数

- 零个或多个固定输入参数；允许用 `~` 忽略输入，例如 `function f(x, ~)`

- `varargin` 可变输入参数

- `varargout` 可变输出参数

- 函数体中的 `global` 和 `persistent` 声明

- 函数内的嵌套函数定义

### **3\.2 嵌套函数**



```MATLAB
function y = outer(x)
    y = inner(x);

    function z = inner(a)
        z = a + x;
    end
end
```



嵌套函数可访问外层函数工作区中的变量。语法上应区分嵌套函数与文件级局部函数；闭包捕获和变量共享方式由运行时语义定义。



### **3\.3 动态调用**



- 普通调用：`f(x)`

- 调用实参中的名称值形式：`f(x, Option=value)`；此处的 `=` 属于实参语法，不能解析为赋值语句

- 句柄调用：`h(x)`

- `feval(f, x)`：通过函数名或函数句柄调用

- `eval(expr)`：执行字符串中的 M 代码

- 命令式调用：`load file.mat`；实参按字符向量传入，语法范围仅限独立语句

`eval` 的求值工作区、结果返回和错误传播需要单独定义运行时规则。`varargin`、`varargout` 也需要在函数调用时参与实际参数绑定和输出数量处理。



## **4\. 类定义**



支持类定义、属性块、方法块、构造函数与简单单继承：



```MATLAB
classdef Child < Parent
    properties
        Value = 0
    end

    methods
        function obj = Child(value)
            obj.Value = value;
        end

        function result = getValue(obj)
            result = obj.Value;
        end
    end
end
```



第一阶段类语法范围：



- `classdef ClassName` 类定义

- 单一父类形式：`classdef Child < Parent`

- 值类与句柄类：普通 `classdef` 定义值类；`classdef Counter < handle` 定义句柄类。两者的赋值、传参与方法修改行为由运行时语义定义

- `properties` 属性块及属性默认值；支持 `properties (Constant)` 常量属性块和 `properties (Dependent)` 依赖属性块

- `methods` 方法块；支持 `methods (Static)` 静态方法块与 `methods (Access = private)` 私有方法块

- 属性访问方法：`function value = get.PropertyName(obj)`；`function obj = set.PropertyName(obj, value)` 用于值类，`function set.PropertyName(obj, value)` 用于句柄类

- 构造函数

- 实例方法定义与调用：`obj.method(args)`

- 属性读取和赋值：`obj.Property`、`obj.Property = value`

- 继承父类的基础属性与方法

- 显式调用父类构造函数：`obj = obj@Parent(args)`，用于向父类构造函数传递参数

- 显式调用父类方法：`method@Parent(obj, args)`，用于在子类方法中调用父类实现

除私有方法以外的访问控制、属性验证器、事件、枚举类、多重继承与抽象方法暂不纳入第一阶段。



## **5\. 文件级结构**



源文件分为以下顶层形式：



1\. **脚本文件**：由脚本语句组成，可按约定包含文件级局部函数。

2\. **函数文件**：包含主函数，并可包含文件级局部函数；函数体内可定义嵌套函数。

3\. **类文件**：包含一个 \`classdef\`，类中包含属性块和方法块。



第一阶段应明确限制顶层形式的组合规则，例如类文件不与脚本主体混用，函数文件的主函数名与文件名匹配规则由解释器另行规定。



## **6\. 第一阶段暂不支持**



- 并行语法：`parfor`、`spmd`

- 完整 MATLAB 元类与完整类系统特性

- 多重继承、属性验证器、事件、枚举类

- `arguments` 参数验证块

普通内置函数（例如 `deal`）不需要专门语法，可通过内置函数库提供。



## **7\. 需要配套定义的运行时语义**



以下项目语法规模不大，但若缺少运行时规则，会使程序行为不完整：



- 函数调用与数组索引的区分

- 多输出赋值及 `nargin`、`nargout` 的值

- `varargin`、`varargout` 的绑定与展开

- `global` 的共享工作区及变量创建规则

- `persistent` 的初始化时机与保留范围

- 嵌套函数对外层变量的捕获与写入行为

- `eval` 执行代码时使用的工作区

- 类实例、构造函数、属性访问和继承方法的分派规则

- 值类与句柄类的复制、共享及方法修改规则

- 命名空间限定名、`import` 与局部变量同名时的名称查找规则

- 名称值实参的绑定，以及静态方法、私有方法和常量属性的访问规则

- 依赖属性的读取与赋值如何调用 `get.PropertyName`、`set.PropertyName`，以及依赖属性本身不存储值的规则

