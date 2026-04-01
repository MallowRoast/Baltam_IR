function test15_func
a = 1.1;
b = 1.2;
f2()
end

function f2
a = 2.1;
b = 2.2;
a_base = 0.1;
b_base = 0.2;
a_caller = 1.1;
b_caller = 1.2;

y_base = evalin('base', 'a^2 + b + 1');
if y_base ~= a_base^2 + b_base + 1
    error('1');
end

y_caller = evalin('caller', 'a^2 + b + 1');
if y_caller ~= a_caller^2 + b_caller + 1
    error('2');
end

y = eval('a^2 + b + 1');
if y ~= a^2 + b + 1
    error('3');
end

% 字符串为非字面量则解析为普通的 node_multiple_func(eval)
% my_str = strrep('aaa^2 + b + 1', 'aaa', 'a');
% y = eval(my_str);
end
