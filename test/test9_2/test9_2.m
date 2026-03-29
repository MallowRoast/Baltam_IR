% for 循环的 continue 和 break
function test9_2
for i = 1:10
    a = rand(1,1);
    if a < 0.5
        continue
    elseif a < 0.6
        break
    end
end
end
