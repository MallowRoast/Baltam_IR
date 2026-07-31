% TEST0 覆盖脚本 lowering / print 的闭环。
%
% 这个用例用于验证 script 场景下的几个关键点：
% 1. 普通名字访问会 lowering 成 workspace 读写，而不是静态 slot 读写
% 2. sin(a) 在脚本中保留源码层圆括号应用歧义，因此应生成 apply，而不是直接变成 call
% 3. 继续覆盖基本算术、if / else，以及源码行号注释打印
%
% 文件尾部的 local 函数用于让 runtime smoke 在保留单一 test0.m 夹具的同时，
% 覆盖脚本执行期间创建函数栈帧的路径。

a = 1 + 2;
b = sin(a);

if b > 0
    c = b * 2;
else
    c = 0;
end

d = test0_local(a);

function y = test0_local(x)
y = x;
end
