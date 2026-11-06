function x = test2(b)
    x = 1;
    for i = 1:b
        x = innerFunction(x);
    end
end

function x = innerFunction(x)
    x = 1+x;
end
