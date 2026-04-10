% 写时复制（暂时不分析后继只静态分析引用数）
function test30
x = 1;
x = [1 2; 3 4] * x; % 允许变量改变类型
x(2, 2) = 4.1;
y = x;
y(1, 2) = 2.1; % COW

% 首先，每个分支中需要有自己的一份 @数字 被谁引用的数据，不能混用
% 解决方法：不记录 @数字 被谁引用的数据，

if rand() < 0.3
    z = x; % 搜索后继发现一个被写之后另一个被读，需要 COW
elseif rand() < 0.6
    z = y; % 搜索后继发现一个被写之后另一个被读，需要 COW
else
    z = [3 4; 5 6];
end
z(2, 1) = 1.7; % 需要在 if 和 elseif 最后插入 COW，而 else 中不需要
x(2, 1) = 1.5;
y(2, 1) = 1.6;

% 检查
if (~isequal(x, [1 2; 1.5 4.1]))
    error('1');
end
if (~isequal(y, [1 2.1; 1.6 4.1]))
    error('2');
end
if (~isequal(z, [1 2; 1.7 4.1]) && ~isequal(z, [1 2.1; 1.7 4.1]) && ~isequal(z, [3 4; 1.7 6]))
    error('3');
end
end
