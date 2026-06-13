% TEST4_2 覆盖 for / while 循环中嵌套 switch / case / otherwise。
%
% 用例重点是验证 switch.end 正常回到外层 for.latch / while.header，
% 且 case 之间没有 fallthrough。

s = 0;

for i = 1:4
    switch i
        case 1
            s = s + 10;
        otherwise
            s = s - 1;
    end
end

j = 0;

while j < 4
    j = j + 1;

    switch j
        case 1
            s = s + 10;
        otherwise
            s = s - 1;
    end
end
