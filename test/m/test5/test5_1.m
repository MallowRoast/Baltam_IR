function z = test5_1(x)
% TEST5_1 覆盖匿名函数句柄、捕获值和 value_apply 调用。
%
% 期望语义：
% 1. `f = @(t) t + y` 在构造时捕获当前 `y`
% 2. 后续 `y = 10` 不影响 `f` 内部看到的捕获值
% 3. `f(x)` 仍通过运行时 function handle value_apply 分派

y = 1;
f = @(t) t + y;
y = 10;
z = f(x);
end
