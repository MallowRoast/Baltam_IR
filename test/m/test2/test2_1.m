% TEST2_1 在 for 循环中增加 continue / break。
%
% 这个用例验证循环体内控制流语句的 lowering：
% 1. `continue` 应跳到 for.latch，仍然执行内部 iter_index 自增
% 2. `break` 应跳到 for.end，直接离开当前循环
% 3. 两者都不应生成用户可见的 Matlab 调用或写入内部状态变量

s = 0;
for i = 1:10
    if i == 3
        continue;
    end

    if i == 8
        break;
    end

    s = s + i;
end
