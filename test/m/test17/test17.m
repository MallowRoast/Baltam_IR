% 函数嵌套
% https://www.mathworks.com/help/matlab/matlab_prog/nested-functions.html
% 北太解释器暂时不支持，但 Matlab 可以运行

function test17

a = 2;
b = -1;

% 2*3 - 1 = 5
if f2(3) ~= 5
    error('1');
end

a = 3;
b = -2;

% 3*4 - 2 = 10
if f2(4) ~= 10
    error('2')
end

% 内层函数可以访问外层函数中的变量（类似于全局变量）
% 支持多层嵌套
function y = f2(x)
    y = a*x + b;
end

end
