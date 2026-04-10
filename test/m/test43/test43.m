% C++ 重构 BAIR（同 test1.m）

% 入参都设为 double
function ret = test43(arg1, arg2)

% 输出
disp('hello, world');

% 修改入参
arg2 = 3;

% 简单表达式
a = 1; b = 2;
c = a + b;

% 判断
if c < 0
    d = b + sqrt(-c);
elseif c > 0
    d = a + sqrt(c);
else
    % 异常退出
    error('c = 0.');
end

% 循环
for n = 1:5
    c = c + n;
end

disp('c = ');
disp(c)

disp('d = ');
disp(d)

% 子函数调用
e = f1(a, b);

disp('e = ');
disp(e)

if arg2 ~= 3
    error('0');
end
if c ~= 18
    error('1');
end
if d ~= 1 + sqrt(3)
    error('2');
end
if e ~= 5
    error('3');
end

% 正常退出
ret = 0;
end

% 子函数
function z = f1(x, y)
z = x^2 + y^2;
end
