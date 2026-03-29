% 更多矩阵操作
function test2_1()
    N = 3;
    X = rand(N,N);
    Y = zeros(N,N);
    Y(2,2) = 1;
    a = X(2,2);
    Y(2,2) = numel(2);
    Y(2,2) = X(2,2);
    
    for i = 1:N
        for j = 1:N
            Y(i,j) = X(j,i);
        end
    end
    if ~isequal(Y, X.')
        error('1');
    end
    
    X = zeros(3, 5);
    Y = zeros(3, 5);
    [X(2:3,1:2:5), Y(:,[3,5])] = myfun(ones(2,3), ones(3,2));
    disp(X);
    disp(Y);
    if ~(all(all(X(2:3,1:2:5) == 2)) && all(all(Y(:,[3,5]) == 2)))
        error('2');
    end
    if ~(numel(find(X == 2)) == 6 && numel(find(Y == 2)) == 6)
        error('3');
    end

    % 第 k 个循环中，循环变量应该区 cc(:,k)
    cc = rand(3,4,5,2);
    k = 1;
    for c = cc
        if ~isequal(c, cc(:,k))
            error('5');
        end
        k = k + 1;
    end
end

function [X1, Y1] = myfun(X, Y)
    X1 = X + 1;
    Y1 = Y + 1;
end
