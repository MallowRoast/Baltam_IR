% 静态结构体 (Struct) 支持测试
% 要求：结构体在创建时定义全部成员，运行过程中不增删成员、不改变成员类型
function test28

% 创建结构体（一次性定义所有字段）
disp('创建结构体');
mat = [1 2; 3 4];
s = struct('a', 1.23, 'b', 'abc', 'c', mat);
disp(s);
if s.a ~= 1.23 || ~strcmp(s.b, 'abc') || ~isequal(s.c, [1 2; 3 4])
    error('1');
end
if ~isequal(mat, [1 2; 3 4])
    error('1.1');
end

% 修改结构体字段中的元素（仅修改内容，不改变结构和类型）
s.c(2, 1) = s.c(2, 1) + 100;
if s.c(2, 1) ~= 103
    error('2');
end

% 复制和修改
disp('复制和修改');
s1 = s;
s1.a = 2.34;
if s1.a ~= 2.34 || s.a ~= 1.23
    error('3');
end

% 作为函数入参
disp('作为函数入参');
s2 = f2(1, s, 3);
if s.a ~= 1.23 || ~strcmp(s.b, 'abc') || ~isequal(s.c, [1 2; 103 4])
    error('4');
end
if s2.a ~= 3.21 || ~strcmp(s2.b, 'cba') || ~isequal(s2.c, [4 3; 2 1])
    error('5');
end

end

% 子函数
function out = f2(a, in, b)
if in.a ~= 1.23 || ~strcmp(in.b, 'abc') || ~isequal(in.c, [1 2; 103 4])
    error('3.1');
end

% 构造一个新结构体（字段与原结构体一致）
out = struct('a', 3.21, 'b', 'cba', 'c', [4 3; 2 1]);
end