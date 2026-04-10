% 函数句柄

function test12
    fh = @sin;
    fh; % 和普通函数不同，如果没有括号就肯定不是函数调用
    fh1 = fh;
    if fh(1) ~= sin(1) || fh1(1) ~= sin(1)
        error('0');
    end
    a = 0; b = pi; N = 1000;
    disp('fh = '); disp(fh);
    I1 = my_integral(fh, a, b, N);
    if abs(I1 - 2) > 2e-6
       error('1');
    end
    I1 = my_integral(@sin, a, b, N);
    if abs(I1 - 2) > 2e-6
       error('1.1');
    end

    fh = @(x)sin(x).^2;
    I2 = my_integral(fh, a, b, N);
    if abs(I2 - pi/2) > 3e-15
        error('2');
    end
    I2 = my_integral(@(x)sin(x).^2, a, b, N);
    if abs(I2 - pi/2) > 3e-15
        error('2.1');
    end

    fh = @(x,n)sin(x).^n;
    disp('fh = '); disp(fh);

    if fh(pi/2, 100) ~= 1
        error('3');
    end

    fh2 = @(x)fh(x, 2); % sin^2
    if fh2(pi/4) ~= sin(pi/4)^2
        error('4');
    end

    f_int = @my_integral;
    I3 = f_int(fh2, a, b, N);
    if abs(I3 - pi/2) > 3e-15
        error('5');
    end
    
    f_int_ab = @(func)my_integral(func, a, b, N);
    I4 = f_int_ab(fh2);
    if abs(I4 - pi/2) > 3e-15
        error('6');
    end
    
    f_int_ab = @(func)f_int(func, a, b, N) + 1;
    a = 1000; b = 2000; % 事后改变 a b 不会影响函数句柄的定义
    I5 = f_int_ab(fh2);
    if abs(I5 - pi/2 - 1) > 3e-15
        error('7');
    end
    
    fh = @() 1 + 1;
    if fh() ~= 2
        error('8');
    end

    % 测试 my_integral(矩阵, a, b, N)
    A = 100:107;
    if my_integral(A, 2, 5, 4) ~= 307.5
        error('9');
    end

    % 测试函数句柄变量或矩阵变量存在同名函数的情况
    foo = @sin;
    if foo(3) ~= sin(3)
        error('10');
    end

    fh3 = @(c, inds)c{inds};
    c3 = {1,2,3,4};
    [a, b] = fh3(c3, 2:3); % 天元暂时有 bug，按 Matlab 生成
    if a ~= 2 || b ~= 3
        error('11');
    end

    fh4 = @(x, y) deal(x, y);
    [a, b] = fh4(1, 2);
    if a ~= 1 || b ~= 2
        error('12');
    end

    % 测试 set_doub() 非 bex 原型
    fh5 = @(x) 3.14;
    a = fh5(0);
    if a ~= 3.14
        error('13');
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
