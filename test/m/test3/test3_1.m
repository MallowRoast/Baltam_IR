% TEST3_1 在 while 循环中增加 continue / break。
%
% 这个用例验证 while lowering 复用 loop context 栈：
% 1. `continue` 应跳到 while.latch，再统一回到 while.header 重新判断条件
% 2. `break` 应跳到 while.end，直接离开当前循环
% 3. 普通路径仍应从 while.body fallthrough 到 while.latch

i = 0;
s = 0;
while i < 10
    i = i + 1;

    if i == 3
        continue;
    end

    if i == 8
        break;
    end

    s = s + i;
end
