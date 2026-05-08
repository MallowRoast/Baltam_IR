% TEST3_3 覆盖 for / while 相互嵌套时 break / continue 的目标选择。
%
% 这个用例验证混合嵌套循环中最近一层 loop context 的选择：
% 1. 外层 for 的 `break` 应跳到外层 for.end
% 2. 内层 while 的 `continue` 应跳到内层 while.latch
% 3. 外层 while 的 `continue` 应跳到外层 while.latch
% 4. 内层 for 的 `break` 应跳到内层 for.end

s = 0;

for i = 1:4
    if i == 4
        break;
    end

    j = 0;
    while j < 3
        j = j + 1;
        if j == 2
            continue;
        end

        s = s + i * j;
    end
end

k = 0;
while k < 4
    k = k + 1;
    if k == 2
        continue;
    end

    for m = 1:3
        if m == 2
            break;
        end

        s = s + k * m;
    end
end
