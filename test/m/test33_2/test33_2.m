% 测试逐个元素运算的内建函数（通用模板和生成函数）
function test33_2
m = 2; n = 3;
a = rand(m, n);
b = rand(m, n);
% TODO: 作为一个示范，这里的 a + b 已经在 gen_c.py 中使用 gen_c_helper_generic_func_1mat_each_elm 实现。 其他函数也要这么做
c1 = a + b;
for i = 1:m*n
    if c1(i) ~= a(i) + b(i)
        error('1')
    end
end
end
