% 这里演示 @-文件夹 可以用于函数重载，但只能根据第一个入参类型单重派发。
% Matlab 支持，天元暂不支持
function test29
    my_f(1, 1001)
    % id: 1001
    % This is a double: 1
    my_f('a', 1002)
    % id: 1002
    % This is a char: a
    my_f(int32(3), 1003)
    % id: 1003
    % This is an int32: 3
end
