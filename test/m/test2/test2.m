% TEST2 覆盖脚本中的 for 循环 lowering / print 闭环。
%
% 这个用例用于验证当前 for lowering 的几个关键点：
% 1. 迭代表达式 `1:10` 先 lower 成普通 `call @colon(...)`，不标记为 internal
% 2. `foreach_init` 返回循环不变量 state / max_iter，其中 max_iter 类型静态为 int64
% 3. 只有 `__for_idx` 需要 internal_local slot，并且该 slot 类型固定为 int64
% 4. header 比较和 latch 自增使用 internal index primitive，不参与 Matlab 运算符重载
% 5. 循环变量 i 每轮开始写入 workspace，循环体里的 s = s + i 仍按脚本 workspace 读写

s = 0;
for i = 1:10
    s = s + i;
end
