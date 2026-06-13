% TEST4 覆盖简单 switch / case / otherwise，以及没有 otherwise 的 switch。
%
% 目标 CFG 形状见 doc/switch_lowering_design.md。

x = 2;
y = 0;

switch x
    case 1
        y = 10;
    otherwise
        y = -1;
end

z = 3;
w = 0;

switch z
    case 1
        w = 10;
    case 2
        w = 20;
end
