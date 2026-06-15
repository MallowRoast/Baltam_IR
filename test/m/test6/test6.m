function y = test6(x)
% TEST6 覆盖短路逻辑 && / || 的 CFG lowering。
%
% `&&` 和 `||` 不能 lower 成普通 binary，因为右侧表达式必须按左侧结果决定是否执行。

a = x > 0;
b = x > 10;

c = a && b;
d = a || (x - 1 > 0);

if c || d
    y = 1;
else
    y = 0;
end
end
