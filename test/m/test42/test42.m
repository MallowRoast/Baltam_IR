% end 关键字
function test42

b = zeros(5, 6, 3, 4); % numel(b) = 360

% end 直接作为整个下标
b = zeros(5, 6, 3, 4);
b(1, end, 2, 1) = 1;
c = b(1, end, 2, 1);
check(b, c, 1, 1);

b = zeros(5, 6, 3, 4);
b(end, 3, 2, end) = 2;
c = b(end, 3, 2, end);
check(b, c, 2, 1);

% end 作为算符参数(:, +, - 等）
b = zeros(5, 6, 3, 4);
b(1, 4:end, 1, 1) = 3;
c = b(1, 4:end, 1, 1);
check(b, c, 3, 3);

b = zeros(5, 6, 3, 4);
b(1:2, 4:end, 3:4, end) = 4;
c = b(1:2, 4:end, 3:4, end);
check(b, c, 4, 12);

b = zeros(5, 6, 3, 4);
b(:, 1, end-1, end-3:end-1) = 5;
c = b(:, 1, end-1, end-3:end-1);
check(b, c, 5, 15);

b = zeros(5, 6, 3, 4);
b(1, end/3*2:4, 1, end-3+end/2) = 6;
c = b(1, end/3*2:4, 1, end-3+end/2);
check(b, c, 6, 1);

% 单参数索引(共 360 个元素）
b = zeros(5, 6, 3, 4);
b(end-3-end/2) = 7;
c =b(end-3-end/2);
check(b, c, 7, 1);

b = zeros(5, 6, 3, 4);
b(end*2/3:end-3-end/2) = 8;
c = b(end*2/3:end-3-end/2);
check(b, c, 8, 0);

% 函数调用与算符等复杂表达式
b = zeros(5, 6, 3, 4);
b(1, end/3*2:5, 1, end-3+end/2) = 9;
c = b(1, end/3*2:5, 1, end-3+end/2);
check(b, c, 9, 2);

b = zeros(5, 6, 3, 4);
b(2:mod(end+2,31)) = 10; % end=360, b(2:21)
c = b(2:mod(end+2,31));
check(b, c, 10, 20);

b(:) = 1:numel(b);
B = [1,2,3; 4,5,6]; % B(5) = 3
if (B(b(end-355)) ~= 3)
    error('4');
end

% 入参实参数小于矩阵维数且最后一个入参中含有 end
% 此时 end 代表剩余维度的元素总数
b = zeros(5, 6, 3, 4);
b(:, 2:3, 6:end/2+1) = 11; % end=12, b(:,2:3,6:7)
c = b(:, 2:3, 6:end/2+1);
check(b, c, 11, 20);

% 矩阵有同名函数
myname = 1:10;
if myname(10-end/2) ~= 5
    error('5');
end

end

%function ret = myname(arg)
%    ret = arg + 10;
%end

% 检查 B, C 中指定非零值的数量
function check(B, C, num, Nnum_expect)
% 检查 B
Nzero = numel(find(B == 0));
Nnum = numel(find(B == num));
if Nnum ~= Nnum_expect
    error('0');
end
if Nzero + Nnum ~= numel(B)
    error('1');
end
% 检查 C
if Nnum ~= numel(C)
    error('2');
end
if any(C(:) ~= num)
    error('3');
end
end
