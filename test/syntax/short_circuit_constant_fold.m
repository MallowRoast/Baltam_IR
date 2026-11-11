function short_circuit_constant_fold()
    x = 1;
    y = 0;
    z = x && y;  % 右侧只在左侧为真时求值，汇合后写 z。
    w = x || y;  % 右侧只在左侧为假时求值，直接写 w。
    q = (x && y) || x;  % 嵌套短路通过 PHI 汇合后写 q。
    if x && y  % 条件不需要结果 slot。
        a = 1;
    else
        a = 2;
    end
end
