function y = test5(x)
% TEST5 覆盖具名函数句柄、矩阵创建和下标访问。
%
% 这个用例面向后续 function handle / matrix / indexing lowering：
% 1. `f = @sin` 构造具名函数句柄
% 2. `f(x)` 调用函数句柄
% 3. `[ ... ]` 构造矩阵字面量
% 4. `A(i, j)` 读取矩阵元素
% 5. `A(i, j) = v` 写入矩阵元素

f = @sin;
a = f(x);

A = [1, 2, 3; 4, 5, 6];
b = A(1, 2);
A(2, 3) = a + b;

y = A(2, 3);
end
