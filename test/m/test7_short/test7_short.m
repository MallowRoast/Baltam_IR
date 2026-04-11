% 测试 || 和 && 的代码生成
% 只覆盖短路语义本身，不覆盖复杂 setter 左值

function test7_short
a = 1:3;

if ~(numel(a) < 4 || a(4) == 5)
    error('1');
end

b = (numel(a) < 4);
c = (a(3) == 5);
if b || c
    ;
else
    error('1.2');
end

if numel(a) > 3 && a(4) == 5
    error('2');
end

b = (numel(a) > 3);
c = (a(3) == 5);
if b && c
    error('2.2');
end

if ~(numel(a) < 4 || a(4) == 5) || numel(a) > 3 && a(4) == 5
    error('3');
end

a = ~(numel(a) < 4 || a(4) == 5);
b = (numel(a) > 3 && a(4) == 5);
if a || b
    error('3.2');
end

% 短路算符的临时变量未必是 _a0
% 短路算符可能出现在函数入参
a = 1; b = 2;
myfunc(a + b, a && b);

x = (numel(a) < 4 || a(4) == 5) + 1;
y = (numel(a) > 3 && a(4) == 5) * 2;
if x == 1 || y ~= 0
    error('3.3');
end

% || 和 && 的结果应该始终为 logical 类型
d = [(42 || 0), (0 || 42), (42 && 0), (0 && 42)];
if ~islogical(d)
    error('3.9');
end
if ~strcmp(class(true), 'logical')
    error('3.10');
end

% 矩阵逻辑算符
a = 1:5;
if a < 5
    error('3.11')
end
end

function myfunc(x, y)
    if x ~= 3
        error('4');
    end
    if ~y
        error('5');
    end
end
