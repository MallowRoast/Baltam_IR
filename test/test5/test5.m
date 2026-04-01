% 函数和算符大全
% 所有需要支持的都要测试

function test5
    
    A = [2,3,4;6,1,2;6,6,6];
    B = [6,9,2;4,6,9;2,3,4];
    a = [3,6;1,7];
    b = [8,4;3,2];
    
    
    aa1 = 4;
    aa2 = 6;
    
    % 位运算符
    b1 = bitand(aa1, aa2);
    b2 = bitor(aa1, aa2);
    b3 = bitxor(aa1, aa2);
    b4 = bitcmp(aa1);
    b5 = bitshift(aa1, aa2);
    disp(b1);
    disp(b2);
    disp(b3);
    disp(b4);
    disp(b5);
    
    % 关系算符
    a1 = eq(aa1, aa2);
    disp(a1);
    a1 = aa1 == aa2;
    disp(a1);
    
    a2 = ne(aa1, aa2);
    disp(a2);
    a2 = aa1 ~= aa2;
    disp(a2);
    
    a3 = lt(aa1, aa2);  
    disp(a3);
    a3 = aa1 > aa2;
    disp(a3);
    
    a4 = gt(aa1, aa2);
    disp(a4);
    a4 = aa1 >= aa2;
    disp(a4);
    
    a5 = le(aa1, aa2);
    disp(a5);
    a5 = aa1 < aa2;
    disp(a5);
    
    a6 = ge(aa1, aa2);
    disp(a6);
    a6 = aa1 <= aa2;
    disp(a6);
    
    %其他算符
    c1 = size(a);
    disp(c1);
    c2 = length(a);
    disp(c2);
    c3 = sum(a);
    disp(c3);
    c4 = mean(a);
    disp(c4);
    c5 = max(b);
    disp(c5);
    c6 = min(b);
    disp(c6);
    c7 = median(b);
    disp(c7);
    c8 = std(b);
    disp(c8);
    
    % 指数及对数算符
    d1 = exp(a);
    disp(d1);
    d2 = log(a);
    disp(d2);
    d3 = log10(b);
    disp(d3);
    d4 = log2(b);
    disp(d4);
    
    
    % 算术运算符
    x1 = plus(A,B);
    disp(x1);
    x1 = A + B;
    disp(x1);
    
    x2 = uplus(A);
    disp(x2);
    x2 = +A;
    disp(x2);
    
    x3 = minus(A,B);
    disp(x3);
    x3 = A - B;
    disp(x3);
    
    x4 = uminus(A);
    disp(x4);
    x4 = -A;
    disp(x4);
    
    x5 = times(A,B);
    disp(x5);
    x5 = A.*B;
    disp(x5);
    
    x6 = mtimes(A,B);
    disp(x6);
    x6 = A * B;
    disp(x6);
    
    x7 = rdivide(A,B);
    disp(x7);
    x7 = A ./ B;
    disp(x7);
    
    x8 = mrdivide(B,A);
    disp(x8);
    x8 = B / A;
    disp(x8);
    
    x9 = ldivide(B,A);
    disp(x9);
    x9 = B .\ A;
    disp(x9);
    
    x10 = mldivide(A,B);
    disp(x10);
    x10 = A \ B;
    disp(x10);
    
    x11 = power(A,B);
    disp(x11);
    x11 = A .^ B;
    disp(x11);
    
    x12 = mpower(A,aa2);
    disp(x12);
    x12 = A ^ aa2;
    disp(x12);
    
    x13 = transpose(A);
    disp(x13);
    X13 = A.';
    disp(x13);
    
    x14 = ctranspose(A);
    disp(x14);
    x14 = A';
    disp(x14);
    
    % 逻辑运算符
    e1 = and(A,B);
    disp(e1);
    e1 = A & B;
    disp(e1);
    
    e2 = or(A,B);
    disp(e2);
    e2 = A | B;
    disp(e2);
    
    e3 = not(A);
    disp(e3);
    e3 = ~A;
    disp(e3);
end
