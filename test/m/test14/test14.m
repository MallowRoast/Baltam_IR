% C 语言生成 test2.m
function test14

% 简单矩阵操作
disp("简单矩阵操作");
a = zeros(2,2)+1;
b = zeros(2,3)+2;
c = zeros(2,5)+3;
d = [a b; c];
d([2,4], 1:2:5) = 4;
d1 = [1     1     2     2     2
     4     1     4     2     4
     3     3     3     3     3
     4     3     4     3     4];
if ~isequal(d, d1)
    disp('d = ');
    disp(d);
    error('简单矩阵操作');
end

% 加减乘除、取矩阵元
disp("加减乘除、取矩阵元");
a = rand(3, 4);
b = rand(3, 4);
c = sqrt(a.^2 + b.^2);
for i1 = 1:3
    i = i1(1); % TODO
    for j1 = 1:4
        j = j1(1); % TODO
        if c(i, j) ~= sqrt(a(i,j)^2 + b(i, j)^2)
            error('c = sqrt(a.^2 + b.^2)');
        end
    end
end

% 解线性方程组
disp("解线性方程组");
A = rand(30,30);
X = rand(30,100);
B = A * X;
X1 = A \ B;
if norm(X-X1) > 1e-10
    error('X1 = A \ B;');
end

% 本征问题
disp("本征问题");
n = 5;
A = rand(5, 5);
A = A + A.';
[V, D] = eig(A);
errs = abs(A*V - V*D);
for i1 = 1:n
    i = i1(1); % TODO
    for j1 = 1:n
        j = j1(1); % TODO
        if errs(i, j) > 1e-10
            error('[V, D] = eig(A)');
        end
    end
end

disp("完成");
end
