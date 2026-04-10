% 变量管理

% RET_ARG 的四种情况
% 实参在出入参列表中任意出现多次
% clear(), exist()
% nargin/nargout varargin
% ans

function test4()
    % TODO: 当实参数量大于形参时，matlab 会在进入被调函数前就报错
    [r1, r2] = f1(1, 2);
    % varargin 测试
    f2();
    f3(1, 2, 3);
    my_args = cell(1, 3);
    my_args0 = my_args;
    f3(my_args{:});
    f4(1);
    f5(1, 2, 3);
    f5(my_args{:});
    if ~isequal(my_args0, my_args)
        error('0');
    end
    f5(1, my_args{2:3});
    if ~isequal(my_args0, my_args)
        error('0.1');
    end
end

function [r1, r2, r3] = f1(a1, a2, a3)
    if nargin ~= 2 || nargout ~= 2
        error('1')
    end
    r1 = a1;
    r2 = a2;
    % TODO: 一旦 nargin/out 在任何地方被赋值，就不能函数体任何地方表示出入参个数了
    % if 0 > 0.5
    %     nargin = 3.14;
    % end
end

function f2(varargin)
    if ~isempty(varargin)
        error('2')
    end
end

function f3(varargin)
    if ~isequal(size(varargin), [1, 3])
        error('3')
    end
end

function f4(A, varargin)
    if ~isempty(varargin)
        error('4')
    end
end

function f5(A, varargin)
    if ~isequal(size(varargin), [1, 2])
        error('5')
    end
    % 不应该改变入参实参
    varargin{1} = 1;
    varargin{2} = 2;
end
