% TEST3 覆盖简单 while 循环。
%
% 这个用例验证 while lowering 的四块 CFG 方案：
% while.header / while.body / while.latch / while.end。

i = 0;
s = 0;
while i < 10
    i = i + 1;
    s = s + i;
end
