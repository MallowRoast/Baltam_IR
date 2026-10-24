function while_loop()
x = 0;
while x < 3              % 每轮重新求值条件。
    x = x + 1;
    for i = 1:2          % 内层 for 有自己的 break/continue 目标。
        if i == 1
            continue
        end
        if x == 2
            break
        end
    end
    if x == 1
        continue          % 回到 while.header。
    end
    if x == 3
        break             % 跳到 while.end。
    end
end
for j = 1:2              % 反向嵌套：for 内部的 while。
    while j < 2
        break             % 只退出内层 while。
    end
    continue              % 跳到外层 for.latch。
end
y = x;
end
