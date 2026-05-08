% TEST2_2 覆盖嵌套 for 循环。
%
% 这个用例验证两层 for lowering 可以同时存在：
% 1. 外层和内层循环各自生成一套 for.preheader/header/body/latch/end CFG
% 2. 两层循环各自拥有独立的 internal iter_index slot
% 3. 内层循环结束后应回到外层 latch，继续推进外层循环

s = 0;
for i = 1:3
    for j = 1:2
        s = s + i * j;
    end
end
