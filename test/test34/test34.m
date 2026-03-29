% nargin/nargout
function test34()
    % TODO: 当实参数量大于形参时，matlab 会在进入被调函数前就报错
    [r1, r2] = f1(1, 2);
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
