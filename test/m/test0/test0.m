% TEST0 覆盖脚本 lowering/print 的闭环。
% 它验证 script 名字访问会走 workspace 读写，并保留调用、分支和源码注释打印。

a = 1 + 2;
b = sin(a);

if b > 0
    c = b * 2;
else
    c = 0;
end
