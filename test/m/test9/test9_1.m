function z = test9_1(a)
% TEST9_1 covers persistent declarations in IR.

persistent x y
x = a;
z = f() + y;
end

function z = f()
persistent x
z = x;
end
