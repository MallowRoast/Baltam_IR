% zeros
function test_zeros
a1 = zeros();
if ~isequal(size(a1), [1, 1])
    error('1');
end

a2 = zeros(2);
if ~isequal(size(a2), [2, 2])
    error('2');
end
if ~all(a2 == 0)
    error('3');
end

a3 = zeros(1, 1, 1);
if ~isequal(size(a3), [1, 1])
    error('4');
end
if a3 ~= 0
    error('5');
end
end
