% 如何确定一个名字是变量还是函数？

function test10
f1([8,9]);

% 函数中报错：变量 f2 未定义
% f3(1,2);

% Matlab： 报错变量 f2 未定义
% 天元： 警告 f2 由函数变为变量然后闪退
% 代码生成： 允许
% f2(1,2) = f2;
% if f2(1,2) ~= 3
%     error('1')
% end

f2 = sin(1);
var = f2;
if var ~= sin(1)
    error('2');
end

if f2 + f2 ~= 2*sin(1) || f2 * f2 ~= sin(1)^2
    error('4');
end
f2 = [1,2];
f2;
f2 = f2;
if f2(1,2) ~= 2
    error('5');
end

% pi(1); % 报错 “参数过多”，pi 并不是系统全局变量而是内建函数
eps(2); % eps 也是内建函数，允许入参

[a5(3), a5(4)] = f5();
if a5(3) ~= 3.3 || a5(4) ~= 4.4
    error('7');
end

[a5(1), a5(2)] = f5; % 这里是 node_asgn，左边是 node_horz_list
if a5(1) ~= 3.3 || a5(2) ~= 4.4
    error('8');
end

if f6(1, 2) ~= 3
    error('9');
end

if f6 ~= 3
    error('10');
end

clear(['f' num2str(2)]);
disp('=== 以下正确行为是报错使用 clear 的变量，显示 3 是错误的 ===');
disp(f2(1, 2));

disp('=== 以下正确行为是报错入参不足 ===');
f1;
f1();
f4(2, 3);

end

% 入参形参必是变量 (即使没有传入实参)
function f1(f2)
if f2(1,2) ~= 9
    error('9');
end
end

function c = f2(a, b)
c = 3;
end

% 出参形参必是变量
function f2 = f3(a, b)
disp(f2(a, b)); % 报错
end

function f4(a, b, f2, c)
disp(f2(1,2));
end

function [a, b] = f5
a = 3.3;
b = 4.4;
end

function c = f6(a, b)
    c = f2()
end
