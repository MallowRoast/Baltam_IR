function for_loop()
    a = 1:3;  % 迭代源只求值一次。
    for i = a  % 外层循环有独立的下标。
        for j = 1:2  % 内层循环也有独立的下标。
            if j == 1
                continue  % 仅跳到内层 latch。
            end
            if i == 2
                break  % 仅跳到内层 end。
            end
            x = j;
        end
        if i == 3
            continue  % 跳到外层 latch。
        end
        if i == 2
            break  % 跳到外层 end。
        end
        y = i;
    end
    z = a;  % 循环退出后继续执行。
end
