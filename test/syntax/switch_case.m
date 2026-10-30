function switch_case()
    x = 2;  % 准备只读取一次的选择值。
    switch x  % 按顺序匹配各个 case。
        case 1  % 第一个匹配候选。
            y = 10;  % 命中第一个 case 时写入。
        case 2  % 第二个匹配候选。
            y = 20;  % 命中第二个 case 时写入。
        otherwise  % 所有 case 都未命中。
            y = 30;  % 默认分支写入。
    end
    z = y;  % 在汇合块读取结果。
end
