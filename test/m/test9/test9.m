function z = test9(a)
% TEST9 covers global declarations in IR.

global x y
x = a;
z = f() + y;
end

function z = f()
global x
z = x;
end
