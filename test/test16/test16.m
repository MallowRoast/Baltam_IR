% BAIR 设计
function test16
x = rand(10, 20) - 0.5;

for i1 = 1:10
    i = i1(1);
    if x(i, 1) < 0
        for j1 = 1:20
            j = j1(1);
            x(i, j) = -x(i, j);
        end
    elseif x(i, 1) == 0
        break
    end
end
end
