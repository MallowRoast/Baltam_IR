function short_circuit(x, y)
    % 参数值在编译时未知，短路控制流不能常量折叠。
    z = x && y;  % 左侧为真时才读取并转换 y。
    w = x || y;  % 左侧为假时才读取并转换 y。
    q = (x && y) || x;  % 嵌套短路，分层使用两路 PHI 汇合。
    if x && y
        a = 1;
    else
        a = 2;
    end
    r = {x && y, x || y};  % 子表达式通过 PHI 产生结果。
end
