% 静态元胞数组 (Tuple) 支持
function test26

% 创建元胞
disp('创建元胞');
mat = [1 2; 3 4];
c = {1.23, 'abc', mat};
disp(c);
if c{1} ~= 1.23 || ~strcmp(c{2}, 'abc') || ~isequal(c{3}, [1 2; 3 4])
    error('1');
end
if ~isequal(mat, [1 2; 3 4])
    error('1.1');
end

% 修改元素
c{3}(2, 1) = c{3}(2, 1) + 100;

if c{3}(2, 1) ~= 103
    error('2');
end

% 复制和修改
disp('复制和修改');
c1 = c;
c1{1} = 2.34;
if c1{1} ~= 2.34 || c{1} ~= 1.23
    error('3');
end

% 作为函数入参
disp('作为函数入参');
c2 = f1(1, c, 3);
if c{1} ~= 1.23 || ~strcmp(c{2}, 'abc') || ~isequal(c{3}, [1 2; 103 4])
    error('4');
end
if c2{1} ~= 3.21 || ~strcmp(c2{2}, 'cba') || ~isequal(c2{3}, [4 3; 2 1])
    error('5');
end
end


% 子函数
function c = f1(a, c, b)
if c{1} ~= 1.23 || ~strcmp(c{2}, 'abc') || ~isequal(c{3}, [1 2; 103 4])
    error('3.1');
end

% 改变类型再改回来
% C代码生成暂时不支持返回值改变类型
/*c = 3;
disp(c);*/

c = {3.21, 'cba', [4 3; 2 1]};
end
