% 测试元胞数组、和函数递归
function test11
    a(:) = 1:12;
    a = reshape(a,2,3,1,2);
    Ndim = ndims(a);
    for i = 1:Ndim+2
        b = flip(a, i);
        b1 = flip1(a, i);
        if ~isequal(b, b1)
            error(num2str(i));
        end
        if i > Ndim && ~isequal(b, a)
            error(num2str(i));
        end
    end
    disp('b,b1最终结果为')
    disp(b);
    disp(b1);
end

% 支持任意维数组的 flip 函数
function x = flip(x, dim)
    if (nargin < 1 || nargin > 2)
        error("flip() 参数个数只能是 1 或 2")
    end
    Ndim = ndims(x);
    if numel(x) <= 1
        return;
    end
    if nargin == 1
        % 确定 dim 的默认值
        for i = 1:Ndim
            if size(x, i) > 1
                dim = i;
            end
        end
    else
        % 检查 dim
        if dim < 1 || ~(dim == round(dim))
            error("dim 必须是正整数")
        elseif size(x, dim) == 1 || dim > Ndim
            return;
        end
    end
    % flip
    inds = cell(1, Ndim);
    for i = 1:Ndim
        if i == dim
            inds{i} = size(x, i):-1:1;
        else
            inds{i} = 1:size(x, i);
        end
    end
    x = x(inds{:});
end

% 支持任意维数组的 flip 函数
% 较为复杂且效率较低的方法
function x = flip1(x, dim)
    if (nargin < 1 || nargin > 2)
        error("flip() 参数个数只能是 1 或 2")
    end
    Ndim = ndims(x);
    if numel(x) <= 1 || dim > Ndim
        return;
    end
    if nargin == 1
        for i = 1:Ndim
            if size(x, i) > 1
                dim = i;
            end
        end
    end
    
    inds = cell(1, Ndim);
    for i = 1:Ndim
        inds{i} = 1;
    end
    inds{dim} = size(x, dim):-1:1;
    if dim == Ndim
        jdim = Ndim-1;
    else
        jdim = Ndim;
    end
    x = flip_recursive(x, dim, jdim, inds);
end

% 做前 jdim 维度的 flip (递归)
function x = flip_recursive(x, dim, jdim, inds)
    Ndim = ndims(x);
    if jdim == 1 || (dim == 1 && jdim == 2)
        for j = 1:size(x, jdim)
            inds{jdim} = j;
            data = x(inds{1:dim-1}, :, inds{dim+1:Ndim});
            x(inds{1:dim-1}, :, inds{dim+1:Ndim}) = data(end:-1:1);
        end
    else
        for j = 1:size(x, jdim)
            inds{jdim} = j;
            if jdim-1 == dim
                jdim1 = jdim-2;
            else
                jdim1 = jdim-1;
            end
            x = flip_recursive(x, dim, jdim1, inds);
        end
    end
end
