% 结构体矩阵
% 与元胞、函数具柄混合测试
function test13_5 % matlab 能运行，天元不能
a.b.c = 1;
a(2).b = 2; % 天元不支持，但可以改用 a(2) = struct('b', 2)
if a(1).b.c ~= 1
    error('1');
end
if a(2).b ~= 2
    error('2');
end
c{2,2} = a;
if c{2,2}(1).b.c ~= 1
    error('3');
end
if c{2,2}(2).b ~= 2
    error('4');
end
a(3).c = 3;
if a(3).c ~= 3
    error('5');
end
if ~isa(a(1).c, 'double') || ~isempty(a(1).c)
    error('6');
end
if ~isa(a(2).c, 'double') || ~isempty(a(2).c)
    error('7');
end

values = num2cell(reshape(1:6, 2, 3));
aa = struct('age', values);

% 扩容结构体数组
aa(5,2).age = 300;

if aa(5,2).age ~= 300
    error('8');
end

% 验证线性索引
if aa(1).age ~= 1 || aa(2).age ~= 2 || aa(6).age ~= 3 || aa(7).age ~= 4 || aa(11).age ~= 5 || aa(12).age ~= 6
    error('9');
end

if aa(1).age ~= aa(1,1).age || aa(2).age ~= aa(2,1).age || aa(6).age ~= aa(1,2).age || aa(7).age ~= aa(2,2).age || aa(11).age ~= aa(1,3).age || aa(12).age ~= aa(2,3).age
    error('10');
end

% 结构体矩阵嵌套
bb = struct('age', 300);
bb = [bb; bb];
cc = struct('bb', bb);
cc = [cc; cc];
if cc(2, 1).bb(2, 1).age ~= 300
    error('11');
end
cc(2, 1).bb(2, 1).age = 301
if cc(2, 1).bb(2, 1).age ~= 301
    error('12');
end
if cc(1, 1).bb(2, 1).age ~= 300 || cc(2, 1).bb(1, 1).age ~= 300
    error('13');
end
end
