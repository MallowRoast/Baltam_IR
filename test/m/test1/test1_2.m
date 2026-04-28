% TEST1_2 在 test1 的脚本骨架上增加一个与 local 函数同名的变量。
%
% 这个用例专门覆盖“脚本中的名字遮蔽”场景。文件尾部仍然定义了
% `f(x) = x + 1`，但脚本主体中先写入了同名变量 `f`。
%
% 当前脚本 lowering 对 `f(a)` 仍保持为 `apply`，不会在 lowering 阶段直接静态分派到
% 文件内 local 函数。这里的 `f = 1` 用来让测试输入显式包含同名 workspace 变量。

a = 1 + 2;
f = 1;
b = f(a);

if b > 0
    c = b * 2;
else
    c = 0;
end

function y = f(x)
y = x + 1;
end
