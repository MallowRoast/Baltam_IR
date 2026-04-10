% 测试 global 关键字
% global MY_GLOBAL_VAR; 函数外的 globa， 暂不支持

function test18()
    MY_GLOBAL_VAR = 10;
    global MY_GLOBAL_VAR MY_GLOBAL_VAR2;
    MY_GLOBAL_VAR2 = 20;
    if ~(MY_GLOBAL_VAR == 10 & MY_GLOBAL_VAR2 == 20)
        error('18.1');
    end

    MY_GLOBAL_VAR = 10;
    if ~(f1() == 20)
        error('18.2');
    end
    
    f2(1)
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
    if MY_GLOBAL_VAR == 30.0 && MY_GLOBAL_VAR == 50.0
        error('18.6');
    end

    f6();
end

function y = f1()
    % 全局变量在使用前必须使用 global 定义
    global MY_GLOBAL_VAR;
    y = MY_GLOBAL_VAR*2;
end

function f2(x)
    global MY_GLOBAL_VAR;
    MY_GLOBAL_VAR = x*2;
end

function f5()
    global MY_GLOBAL_VAR MY_GLOBAL_VAR2;
    temp = MY_GLOBAL_VAR;
    MY_GLOBAL_VAR = MY_GLOBAL_VAR2;
    MY_GLOBAL_VAR2 = temp;
end

function f6()
    global MY_GLOBAL_VAR3;
    if ~(isempty(MY_GLOBAL_VAR3) && isa(MY_GLOBAL_VAR3,'double'))
        error('18.7');
    end
end

%错误写法的部分
function y = f3(MY_GLOBAL_VAR)
    % 这个应该生成本地变量
    y = MY_GLOBAL_VAR*2;
    MY_GLOBAL_VAR = MY_GLOBAL_VAR*2;
end

function MY_GLOBAL_VAR = f4(x)
    % 这个应该生成本地变量
    MY_GLOBAL_VAR = x*2;
end
