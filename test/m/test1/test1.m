% 代码生成的入口函数，arg* 限制为字符串，ret 限制为非负整数
function ret = test1(arg1, arg2)

% 输出
disp('hello, world');

% 修改入参
arg2 = 3;

% 简单表达式
a = 1; b = 2;
c = a + b;

disp('a = '); disp(a);
disp('b = '); disp(b);
disp('c = '); disp(c);

% 判断
if c < 0
    d = b + sqrt(-c);
elseif c > 0
    d = a + sqrt(c);
else
    % 异常退出
    error('c = 0.');
end

disp('d = '); disp(d);
if a ~= 1 | b ~= 2 | c ~= 3 | abs(d-1-sqrt(3)) > 1e-15
    error('1');
end

% 循环
for n = 1:5
    c = c + n;
end

disp('c = ');
disp(c)

% 子函数调用
e = f1(a, b);

disp('e = ');
disp(e)

if c ~= 18 | e ~= 5
    error('2');
end

% 正常退出
ret = 0;
end

% 子函数
function z = f1(x, y)
z = x^2 + y^2;
end
