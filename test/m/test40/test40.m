function test40
% 测试 op_elm_set 和 op_elm_get 函数

ur = 1:5;  % [1,2,3,4,5]

ur(3) = 10;  % 应该变成 [1,2,10,4,5]

if ~isequal(ur, [1,2,10,4,5])
    error('1');
end

val = ur(3);
if ~isequal(val, 10)
    error('2');
end

ur = 1:5;
ur(:) = 2:6;  % 应该变成 [2,3,4,5,6]

if ~isequal(ur, 2:6)
    error('3');
end

ur = 1:5;
ur(:) = 5;  % 应该变成 [5,5,5,5,5]

if ~isequal(ur, [5,5,5,5,5])
    error('4');
end

ur = 1:10;

ur(2:2:8) = 0;  % 应该变成 [1,0,3,0,5,0,7,0,9,10]

if ~isequal(ur, [1,0,3,0,5,0,7,0,9,10])
    error('5');
end

sub_ur = ur(3:5);  % 应该返回 [3,0,5]
if ~isequal(sub_ur, [3,0,5])
    error('6');
end

A = [1,2,3; 4,5,6; 7,8,9];


A(2,3) = 100;  % 应该变成 [1,2,3; 4,5,100; 7,8,9]

expected = [1,2,3; 4,5,100; 7,8,9];
if ~isequal(A, expected)
    error('7');
end

val = A(2,3);
if ~isequal(val, 100)
    error('8');
end


A = [16,2,3,13; 5,11,10,8; 9,7,6,12; 4,14,15,1];  % 4x4魔方阵

A(2:3, 2:3) = [10,20; 30,40];

if ~isequal(A(2,2), 10) || ~isequal(A(2,3), 20) || ~isequal(A(3,2), 30) || ~isequal(A(3,3), 40)
    error('9');
end

sub_A = A(1:2, 3:4);
if ~isequal(sub_A, [3,13;20,8])
    error('10');
end

A = [10,20,30; 40,50,60];

A([1,3,5]) = [100,300,500];  % 应该变成 [100,20,300; 40,500,60]

expected = [100,300,500; 40,50,60];
if ~isequal(A, expected)
    error('11');
end

linear_vals = A([2,4,6]);
if ~isequal(linear_vals, [40,50,60])
    error('12');
end

A = [1,2,3,4,5];

A(end-1:end) = [10,20];  % 应该变成 [1,2,3,10,20]

expected = [1,2,3,10,20];
if ~isequal(A, expected)
    error('13');
end

end_vals = A(end-2:end);
if ~isequal(end_vals, [3,10,20])
    error('14');
end

A = zeros(2,3,2);
A(:,:,1) = [1,2,3; 4,5,6];
A(:,:,2) = [7,8,9; 10,11,12];

A(2,1,2) = 100;  % 应该改变第二个切片的(2,1)位置

if ~isequal(A(2,1,2), 100)
    error('15');
end

disp('所有测试通过');
end
