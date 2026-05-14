function [sum_value, diff_value] = test7(x, y)
% TEST7 覆盖多返回值槽位和多结果调用。
%
% 主函数声明两个返回值，并从 local function 调用中接收两个结果值。
% 这个用例聚焦验证 `[a, b] = f(...)`、`[~, b] = f(...)` lowering，
% 以及包含多个返回槽位的函数签名。

[sum_value, diff_value] = pair_ops(x, y);
[~, diff_value] = pair_ops(sum_value, diff_value);
end

function [s, d] = pair_ops(a, b)
s = a + b;
d = a - b;
end
