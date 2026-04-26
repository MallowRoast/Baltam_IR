% TEST0 覆盖脚本 lowering / print 的闭环。
%
% 这个用例用于验证 script 场景下的几个关键点：
% 1. 普通名字访问会 lowering 成 workspace 读写，而不是静态 slot 读写
% 2. sin(a) 在脚本中保留源码层圆括号应用歧义，因此应生成 apply，而不是直接变成 call
% 3. 继续覆盖基本算术、if / else，以及源码行号注释打印
%
% 这个文件会和 test0_1.m 保持接近的源码骨架，只改变“脚本 vs 函数”这一点，
% 以便更清楚地对比两种工作区语义下的 lowering 差异。

a = 1 + 2;
b = sin(a);

if b > 0
    c = b * 2;
else
    c = 0;
end
