% TEST2_4 覆盖嵌套 for 中内层 continue 和外层 break 的目标选择。
%
% 这个用例验证 loop context 栈在嵌套循环中按最近一层循环生效：
% 1. 内层 `continue` 应跳到内层 for.latch
% 2. 外层 `break` 应跳到外层 for.end
% 3. 内层循环正常结束后仍应回到外层 for.latch

s = 0;
for i = 1:4
    if i == 4
        break;
    end

    for j = 1:3
        if j == 2
            continue;
        end

        s = s + i * j;
    end
end
