% TEST1 在 test0 的脚本骨架上增加 local 函数 sin。
%
% 这个用例专门覆盖“脚本中的 local 函数”场景。虽然文件尾部定义了
% `sin(x) = x + 1`，但脚本主体里的 `sin(a)` 目前仍应保守保持为 `apply`，
% 而不是在 lowering 阶段直接静态分派成 `call mfunc`。
%
% 也就是说，这个用例的重点不是 local 函数体本身，而是验证：
% - 同文件 local 函数已经能被收集到 `MFileUnit`
% - 脚本中的调用点仍保留运行时分派语义
%
% 脚本尾部的 `sin` 只作为“当前文件里确实存在同名 local 函数”的样例：
%   function y = sin(x)
%       y = x + 1;
%   end

a = 1 + 2;
b = sin(a);

if b > 0
    c = b * 2;
else
    c = 0;
end

function y = sin(x)
y = x + 1;
end
