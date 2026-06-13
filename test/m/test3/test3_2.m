% TEST3_2 覆盖 for / while 的相互嵌套。
%
% 这个用例验证两类循环混合嵌套时 loop context 和 CFG 块目标保持独立：
% 1. 外层 for、内层 while：内层 while.end 应回到外层 for.latch
% 2. 外层 while、内层 for：内层 for.end 应回到外层 while.header
% 3. 两类循环的同名基本块应通过标签后缀区分

s = 0;

for i = 1:3
    j = 0;
    while j < 2
        j = j + 1;
        s = s + i * j;
    end
end

k = 0;
while k < 2
    k = k + 1;
    for m = 1:2
        s = s + k * m;
    end
end
