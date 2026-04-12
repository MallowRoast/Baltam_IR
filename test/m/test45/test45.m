% global 索引/元胞回归：
% 覆盖 global 圆括号写回、global 元胞写回，以及 call 输出到 global 索引左值。

function test45()
    global G_VEC G_CELL;

    G_VEC = [1, 2, 3, 4];
    G_VEC(3) = 30;
    if ~isequal(G_VEC, [1, 2, 30, 4])
        error('45.1');
    end

    [G_VEC(2), temp] = f_pair();
    if G_VEC(2) ~= 20 || temp ~= 99
        error('45.2');
    end

    mutate_vec();
    if ~isequal(G_VEC, [11, 20, 30, 40])
        error('45.3');
    end

    G_CELL = {10, 20, 30};
    G_CELL{2} = 200;
    if G_CELL{1} ~= 10 || G_CELL{2} ~= 200 || G_CELL{3} ~= 30
        error('45.4');
    end

    mutate_cell();
    if G_CELL{1} ~= 110 || G_CELL{2} ~= 200 || G_CELL{3} ~= 300
        error('45.5');
    end
end

function mutate_vec()
    global G_VEC;
    G_VEC(1) = G_VEC(1) + 10;
    [G_VEC(4)] = f_scalar(40);
end

function mutate_cell()
    global G_CELL;
    G_CELL{1} = G_CELL{1} + 100;
    G_CELL{3} = f_scalar(300);
end

function [a, b] = f_pair()
    a = 20;
    b = 99;
end

function y = f_scalar(x)
    y = x;
end
