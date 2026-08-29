function acc = test11()
% TEST11 measures a nested loop with tic/toc.

tic();

acc = 0;
n = 400;

for i = 1:n
    for j = 1:n
        acc = acc + i + j;
    end
end

t = toc();
t
end
