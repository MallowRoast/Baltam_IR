function if_else()
x = 1;          % 准备条件变量。
if x             % 根据变量选择分支。
    y = 2;      % 条件为真时写入。
else             % 条件为假时执行。
    y = 3;      % 条件为假时写入。
end
z = y;          % 在汇合块读取结果。
end
