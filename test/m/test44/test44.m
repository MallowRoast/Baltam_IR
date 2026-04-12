% global 基础回归：
% 只覆盖声明后使用、跨函数共享、整对象替换。

function test44()
    global G_VALUE G_VALUE2 G_MATRIX;

    G_VALUE = 10;
    G_VALUE2 = 20;
    if ~(G_VALUE == 10 & G_VALUE2 == 20)
        error('44.1');
    end

    G_VALUE = 10;
    if ~(f1() == 20)
        error('44.2');
    end

    f2(1);
    if ~(G_VALUE == 2)
        error('44.3');
    end

    y = f3(G_VALUE);
    if ~(y == 4) || ~(G_VALUE == 2)
        error('44.4');
    end

    y = f4(2);
    if ~(y == 4) || ~(G_VALUE == 2)
        error('44.5');
    end

    G_VALUE = 50;
    G_VALUE2 = 30;
    f5();
    if ~(G_VALUE == 30 & G_VALUE2 == 50)
        error('44.6');
    end

    G_MATRIX = [1, 2; 3, 4];
    f6();
    if ~isequal(G_MATRIX, [4, 3; 2, 1])
        error('44.7');
    end
end

function y = f1()
    global G_VALUE;
    y = G_VALUE * 2;
end

function f2(x)
    global G_VALUE;
    G_VALUE = x * 2;
end

function y = f3(G_VALUE)
    y = G_VALUE * 2;
    G_VALUE = G_VALUE * 2;
end

function G_VALUE = f4(x)
    G_VALUE = x * 2;
end

function f5()
    global G_VALUE G_VALUE2;
    temp = G_VALUE;
    G_VALUE = G_VALUE2;
    G_VALUE2 = temp;
end

function f6()
    global G_MATRIX;
    G_MATRIX = [4, 3; 2, 1];
end
