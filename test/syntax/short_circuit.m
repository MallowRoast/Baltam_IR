function short_circuit()
x = 1;
y = 0;
z = x && y;             % 右侧只在左侧为真时求值，直接写 z。
w = x || y;             % 右侧只在左侧为假时求值，直接写 w。
q = (x && y) || x;      % 嵌套短路仍只写最终目标 q。
if x && y               % 条件不需要结果 slot。
    a = 1;
else
    a = 2;
end
end
