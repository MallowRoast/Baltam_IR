% builtin.sum 的脚本实现
function test33_1
a = rand(1, 5);
if ~isequal(my_sum(a), sum(a))
    error('1');
end
a = rand(5, 1);
if ~isequal(my_sum(a), sum(a))
    error('2');
end
a = rand(2, 3, 4, 5);
for i = 1:4
    if ~isequal(my_sum(a, i), sum(a, i))
        error(num2str(2+i));
    end
end
end

% TODO: 等 BAIR 迁移到 C++ 后需要把以下函数删除放到 cg_script
function r = my_sum(a, dim)
if nargin == 1
    if isrow(a) || iscolumn(a)
        r = zeros(1, 1);
        r = sum1(a);
        return;
    end
    dim = 1;
end
sz = size(a);
ni = prod1(sz(1:dim-1));
nj = prod1(sz(dim+1:end));
ns = sz(dim)-1;
ni2 = ni * sz(dim);
sz(dim) = 1;
r = zeros(sz); % TODO: 这里应该调用 bair::resize()，等待适配命名空间
for i = 0:ni-1
    for j = 0:nj-1
        start = i + ni2*j + 1;
        step = ni;
        stop = start + ns*step;
        r_start = i + ni*j + 1;
        r(r_start) = sum1(a(start:step:stop));
    end
end
end

% 简化版 sum，输出标量
function r = sum1(a)
r = 0;
for i = 1:numel(a)
    r = r + a(i);
end
end

% 简化版 prod，输出标量
function r = prod1(a)
r = 1;
for i = 1:numel(a)
    r = r * a(i);
end
end

function r = isrow(a)
    r = ndims(a) == 2 && size(a, 1) == 1;
end

function r = iscolumn(a)
    r = ndims(a) == 2 && size(a, 2) == 1;
end
