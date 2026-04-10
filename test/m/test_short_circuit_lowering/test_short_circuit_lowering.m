function y = test_short_circuit_lowering()
a = true;
b = false;

x = a || rhs_should_not_run();
z = b && rhs_should_not_run();

if x && ~z
    y = 1;
else
    y = 0;
end
end

function r = rhs_should_not_run()
error('rhs_should_not_run called');
r = false;
end
