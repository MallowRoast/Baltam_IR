function y = test8(A, fun)
% TEST8 覆盖圆括号索引上下文中的 magic end lowering.
%
% 期望语义：
% 1. `end` 只在已有索引上下文中 lower 成 magic_end IR 节点
% 2. `end - 1` 和 `1:end` 内部的 magic end 仍携带同一层索引上下文
% 3. 二维索引中的 `end` 携带对应维度编号
% 4. 索引赋值中的 `end` 也使用同一套 magic_end 节点
% 5. `A(fun(end))` 中的 `end` 保留内层 `fun(end)` 和外层 `A(...)` 候选链

last_value = A(end);
prev_value = A(end - 1);
prefix_value = A(1:end);
row_tail = A(end, 1);
col_tail = A(1, end);
nested_index = A(fun(end));

A(end) = last_value + prev_value;
end
