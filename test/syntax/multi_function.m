function y = multi_function(x)
y = twice(x);
y = add_one(y);
end

function y = twice(x)
y = x * 2;
end

function y = add_one(x)
y = x + 1;
end
