% 函数句柄
% 从 test12.m 部分选取
function test27
    a = 0;
    b = pi;
    n = 2;
    N = 1000;
    fh = @(x,n)sin(x).^n;
    I2 = my_integral(fh, a, b, N);
    if abs(I2 - pi/2)
        error('2.1');
    end
end

% N 点积分
% 梯形算法
function s = my_integral(func, a, b, N)
    x = linspace(a, b, N);
    % 入参形参后面的圆括号应该生成 op_round()，根据其动态类型决定调用 op_elm() 还是 call_func_handle()
    y = func(x);
    dx = (b-a)/(N-1);
    s = (sum(y) - y(1)/2 - y(end)/2)*dx;
end
