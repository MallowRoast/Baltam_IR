function c = test0_1()
% TEST0_1 覆盖 test0 对应的函数 lowering/print 闭环。
% 它验证返回槽位 c、局部 slot 读写，以及未声明为变量的 sin 会 lowering 成 call。

a = 1 + 2;
b = sin(a);

if b > 0
    c = b * 2;
else
    c = 0;
end
end
