% 动态 COW 函数方案
function test30_2
x = [1 2 3];
y = f1(x);
% clear('x');

y = f2(x);

x = [1 2; 3 4]; % 新数据
y = f3(x); % y 引用 x
end

function y = f1(x)
y = x + 1; % no COW
end

function y = f2(x)
x(2, 2) = 4.2; % COW
y = x + 1;
end

function y = f3(x)
    y = x;
end
