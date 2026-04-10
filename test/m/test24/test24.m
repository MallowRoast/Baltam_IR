% test7.m 的 C 生成
% 有修改，避免不支持的语法

function test24
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

a0 = ~(numel(a) < 4 || a(4) == 5);
b = (numel(a) > 3 && a(4) == 5);
if a0 || b
    error('3.2');
end

% 短路算符的临时变量未必是 _a0
% 短路算符可能出现在函数入参
a1 = 1; b1 = 2;
myfunc(a1 + b1, a1 && b1);

x = (numel(a) < 4 || a(4) == 5) + 1;
y = (numel(a) > 3 && a(4) == 5) * 2;
if x == 1 || y ~= 0
    error('3.3');
end

% 矩阵逻辑算符
a2 = 1:5;
if a2 < 5
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
