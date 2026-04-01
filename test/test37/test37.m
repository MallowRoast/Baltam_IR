% 多出参 m 函数调用测试用例
function test37

M1 = rand(3, 3);
M2 = ones(3, 3) * 2;
M3 = rand(3, 3) + 5;
aa = rand(3, 3);

[A1, B1, C1, D1] = f1(10, 20);

[A2, B2, C2] = f1(M1, 3);

[A3, B3] = f1(4, M2);

c = f2(10, 20);
[a, b] = f2(10, 20);
[g, h, i] = f2(aa, 3);

j = f3(aa, aa);
k = f3(2, 3);

if (D1 - (A1 + B1 + 1)) ~= zeros(3, 3)
    error('1');
end

if (C2 - ((A2 .* B2) + 2 * ones(3, 3))) ~= zeros(3, 3)
    error('2');
end

if (A3 - (4 + 2.5)) ~= zeros(3, 3)
    error('3');
end

if (c - (20 + ones(3, 3))) ~= zeros(3, 3)
    error('4');
end

if (a - (20 + ones(3, 3))) ~= zeros(3, 3)
    error('5');
end

if (b - 10) ~= 0
    error('6');
end

if (g - (3 + ones(3, 3))) ~= zeros(3, 3)
    error('7');
end

if (h - aa) ~= zeros(3, 3)
    error('8');
end

if (i - (aa - 3)) ~= zeros(3, 3)
    error('9');
end

if j  ~= (2*aa)
    error('10');
end

if k  ~= 5
    error('11');
end

disp('测试通过');
end

function [A, B, C, D] = f1(X, Y)
R1 = ones(3, 3);
R2 = ones(3, 3) * 2;
R3 = zeros(3, 3);

Xmat = X + R3 .* R1;
Ymat = Y + R3 .* R2;


A = Xmat + 2.5;
B = Ymat .* R1 + 1;
C = (A .* B) + R2;
D = A + B + 1.0;
end

function [b, a, d, c] = f2(m, n)
aa = ones(3, 3);
a = m;
b = n + aa;
c = (m + n)*aa;
d = m - n;
end

function ff = f3(m, n)
ff = m+n;
end
