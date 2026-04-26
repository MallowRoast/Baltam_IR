function c = test0_1()
% TEST0_1 覆盖与 test0 对应的函数 lowering / print 闭环。
%
% 这个用例用于验证 function 场景下的几个关键点：
% 1. 参数、返回值和局部变量访问会 lowering 成 load_slot / store_slot
% 2. sin(a) 在函数中按函数名分派，因此应生成 call，而不是保留为 apply
% 3. 继续覆盖返回槽位、if / else 和隐式 ret
%
% 这个文件会和 test0.m 保持接近的源码骨架，只改变“函数 vs 脚本”这一点，
% 以便更清楚地暴露名字绑定和调用分派规则上的 lowering 差异。

a = 1 + 2;
b = sin(a);

if b > 0
    c = b * 2;
else
    c = 0;
end
end
