% TEST4_1 覆盖 switch 内嵌 switch。
%
% 用例重点是验证内层 switch.end 回到外层 case 的后续语句，
% 且内外两层 switch 的 case 之间都没有 fallthrough。

x = 2;
y = 3;
s = 0;

switch x
    case 1
        s = 10;
    case 2
        switch y
            case 1
                s = 21;
            case {2, 3}
                s = 23;
            otherwise
                s = -20;
        end

        s = s + 1;
    otherwise
        s = -1;
end
