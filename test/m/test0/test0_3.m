function c = test0_3()
% TEST0_3 在 test0_1 的函数骨架上增加 local 函数 sin / cos / plus / uminus。
%
% 这个用例专门覆盖“函数中的 local 函数分派”和“同名局部变量遮蔽”。
%
% 这里一共验证 4 件事：
% 1. `sin(a)` 在函数里应直接静态分派到 local `sin`
% 2. 一元算子 `-a` 在存在 local `uminus` 时，应静态分派到 local `uminus`
% 3. `cos = 1` 之后，`cos(a)` 不应再命中 local `cos`，而应把 `cos` 当作局部变量
% 4. `plus = 1` 之后，`a = 1 + 2` 不应再命中 local `plus`，而应回退成普通 `add`
%
% 之所以要显式写 `plus = 1` 和 `cos = 1`，是因为这两个赋值不是在测试数值结果，
% 而是在测试 lowering 的“名字优先级”：
% - `plus = 1` 用来验证运算符对应的函数名也会被局部变量遮蔽
% - `cos = 1` 用来验证普通调用名同样会被局部变量遮蔽
%
% 主函数后面的 local 定义则提供这些静态分派目标：
% - `sin(x) = x + 1`
% - `cos(x) = x + 1`
% - `plus(x, y) = x`
% - `uminus(x) = x`

plus = 1;
a = 1 + 2;
b = sin(a);
cos = 1;
d = cos(a);
e = -a;

if b > 0
    c = b * 2;
else
    c = 0;
end
end

function y = sin(x)
y = x + 1;
end

function y = cos(x)
y = x + 1;
end

function z = plus(x, y)
z = x;
end

function y = uminus(x)
y = x;
end
