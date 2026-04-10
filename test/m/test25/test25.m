% 复数支持的 C 生成
% test1_3.m 少量修改而来

function test25
a = 1 + 1i;
if a ~= 1+sqrt(-1 + 0i)
    error('1');
end
b = 1j - 1;
if b ~= sqrt(-1 + 0i)-1
    error('2');
end
c = 2.3i; % node_number, str: "2.3i"
if c ~= 2.3*sqrt(-1 + 0i)
    error('3');
end
d = 2+2.7j; % node_number, str: "2.7j"
if d ~= 2+2.7*sqrt(-1 + 0i)
    error('4');
end

% 赋值
i = 3.3;
a1 = 1 + i;
if a1 ~= 4.3
    error('5');
end

c = 2.3i; % 仍是虚数
if c ~= 2.3*sqrt(-1 + 0i)
    error('6');
end
disp(c);

j = 3.3;
b1 = j - 1;
if b1 ~= 2.3
    error('7');
end
d = 2+2.7j; % 仍是虚数
if d ~= 2+2.7*sqrt(-1 + 0i)
    error('8');
end
disp(d);

f1();
end

function f1
    % 该函数中 i,j 仍然是 builtin 函数
    a = 1 + 1i;
    if a ~= 1+sqrt(-1 + 0i)
        error('9');
    end
    b = 1j - 1;
    if b ~= sqrt(-1 + 0i)-1
        error('10');
    end
end
