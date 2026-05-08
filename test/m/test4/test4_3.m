% TEST4_3 覆盖 switch case 中包裹 for / while 循环。
%
% 用例重点是验证 switch 不建立 loop context，循环内部的 break / continue
% 仍然命中最近一层 for / while，且循环正常结束后回到 switch.end。

mode = 2;
s = 0;
i = 0;

switch mode
    case 1
        for k = 1:5
            if k == 2
                continue;
            end

            if k == 4
                break;
            end

            s = s + k;
        end
    case 2
        while i < 5
            i = i + 1;

            if i == 2
                continue;
            end

            if i == 4
                break;
            end

            s = s + i;
        end
    otherwise
        s = -1;
end
