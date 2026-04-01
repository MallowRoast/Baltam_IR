function test6
    Y = DiagonalMatrix(4);
    disp(Y);
end

function Y = DiagonalMatrix(N)
Y = zeros(N,N);
for i = 1:N
    Y(i,i) = i;
end
end
