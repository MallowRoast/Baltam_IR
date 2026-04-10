% 测试 global 关键字
% global MY_GLOBAL_VAR; 函数外的 global，暂不支持

function test38()
    
    global MY_GLOBAL_VAR MY_GLOBAL_VAR2;
    MY_GLOBAL_VAR = 10;
    MY_GLOBAL_VAR2 = 20;
    if ~(MY_GLOBAL_VAR == 10 & MY_GLOBAL_VAR2 == 20)
        error('18.1');
    end

    MY_GLOBAL_VAR = 10;
    if ~(f1() == 20)
        error('18.2');
    end
    
    f2(1);
    if ~(MY_GLOBAL_VAR == 2)
        error('18.3');
    end
    
    y = f3(MY_GLOBAL_VAR);
    if ~(y == 4) || ~(MY_GLOBAL_VAR == 2)
        error('18.4');
    end

    y = f4(2);
    if ~(y == 4) || ~(MY_GLOBAL_VAR == 2)
        error('18.5');
    end
    
    MY_GLOBAL_VAR = 50;
    MY_GLOBAL_VAR2 = 30;
    f5();
    if MY_GLOBAL_VAR == 30.0 && MY_GLOBAL_VAR2 == 50.0
        % 注意：原代码此处逻辑有误，应检查两个变量是否交换
        % 原写法 "if MY_GLOBAL_VAR == 30.0 && MY_GLOBAL_VAR == 50.0" 永远为假
        % 应改为如下（已修正）
    else
        error('18.6');
    end

     %% ===== 新增：结构体 global 测试 =====
    global MY_STRUCT;
    
    % 初始化结构体：标量 + 矩阵
    MY_STRUCT = struct('scalar', 3.14, 'matrix', [1, 2; 3, 4]);
    
    % 验证初始值（字段分别比较，矩阵用 isequal）
    if ~(MY_STRUCT.scalar == 3.14) || ~isequal(MY_STRUCT.matrix, [1, 2; 3, 4])
        error('18.7');
    end

    % 调用函数修改结构体
    f7();
    if ~(MY_STRUCT.scalar == 6.28) || ~isequal(MY_STRUCT.matrix, [2, 4; 6, 8])
        error('18.8');
    end

    % 调用函数替换整个结构体
    f8();
    if ~(MY_STRUCT.scalar == 100) || ~isequal(MY_STRUCT.matrix, [2, 2; 2, 2])
        error('18.9');
    end

    % 测试传入结构体字段作为参数（不应影响全局）
    temp_val = f9(MY_STRUCT.scalar);
    if temp_val ~= 200 || MY_STRUCT.scalar ~= 100
        error('18.10');
    end
    /*f6();*/
end

%% ===== 新增的辅助函数 =====

function f7()
    global MY_STRUCT;
    MY_STRUCT.scalar = MY_STRUCT.scalar * 2;
    MY_STRUCT.matrix = MY_STRUCT.matrix * 2;
end

function f8()
    global MY_STRUCT;
    MY_STRUCT.scalar = 100;
    MY_STRUCT.matrix = [2,2;2,2];  % 2x2 单位矩阵
end

function y = f9(x)
    % x 是传值，不是全局变量，修改不影响 MY_STRUCT
    y = x * 2;
    x = 999;  % 本地修改，无影响
end

%% ===== 原有函数（保持不变）=====

function y = f1()
    global MY_GLOBAL_VAR;
    y = MY_GLOBAL_VAR*2;
end

function f2(x)
    global MY_GLOBAL_VAR;
    MY_GLOBAL_VAR = x*2;
end

function y = f3(MY_GLOBAL_VAR)
    % 这个应该生成本地变量
    y = MY_GLOBAL_VAR*2;
    MY_GLOBAL_VAR = MY_GLOBAL_VAR*2;
end

function MY_GLOBAL_VAR = f4(x)
    % 这个应该生成本地变量
    MY_GLOBAL_VAR = x*2;
end

function f5()
    global MY_GLOBAL_VAR MY_GLOBAL_VAR2;
    temp = MY_GLOBAL_VAR;
    MY_GLOBAL_VAR = MY_GLOBAL_VAR2;
    MY_GLOBAL_VAR2 = temp;
end

/*
function f6()
    global MY_GLOBAL_VAR3;
    if ~(isempty(MY_GLOBAL_VAR3) && isa(MY_GLOBAL_VAR3,'double'))
        error('18.7');
    end
end
*/
