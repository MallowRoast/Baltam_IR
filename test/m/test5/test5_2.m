% TEST5_2 覆盖脚本中的匿名函数句柄和 workspace 捕获。
%
% 期望语义：
% 1. 脚本变量 `y` 来自当前 workspace，而不是函数 local slot
% 2. `f = @(t) t + y` 在构造时捕获当前 workspace 中 `y` 的值
% 3. 后续 `y = 10` 不影响 `f` 内部看到的捕获值

y = 1;
f = @(t) t + y;
y = 10;
z = f(2);
