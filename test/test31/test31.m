% 函数实参或形参变量名重复（alias）
% 结论：任何情况下入参实参如果不出现在出参实参中就不会被改变。

function test31()
% 形参相同，实参相同
y = 1:4;
y = f1(y);
if ~isequal(y, [1 2 4 4])
    error("0");
end
% 形参相同，实参不同
x = 1:4;
y = f1(x);
if ~isequal(x, [1 2 3 4])
    error("1");
end
if ~isequal(y, [1 2 4 4])
    error("2");
end
% 形参不同，实参相同
x = 1:4;
x = f2(x);
if ~isequal(x, [1 2 4 4])
    error("3");
end
% 形参不同，实参不同
x = 1:4;
y = f2(x);
if ~isequal(x, [1 2 3 4])
    error("4");
end
if ~isequal(y, [1 2 4 4])
    error("5");
end
end

% 形参相同
function X = f1(X)
    X(3) = X(3) + 1;
end

% 形参不同
function Y = f2(X)
    Y = X;
    Y(3) = Y(3) + 1;
end
