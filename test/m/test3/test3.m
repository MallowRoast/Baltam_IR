% 读写文件
function test3
% 请自行修改路径（仅支持绝对路径）
mat_path = 'test3.mat';
txt_path = "test3.txt";
a = [2,3];
b = 'bbb';
a1 = a;
b1 = b;
save(mat_path, 'a', 'b');
clear('a'); clear('b');
load(mat_path, 'a', 'b');
if ~isequal(a, a1) | ~isequal(b, b1)
    error('1');
end

c = magic(3);
writematrix(c, txt_path);
c1 = readmatrix(txt_path);
if max(abs(c(:) - c1(:))) > 1e-4
    error('2');
end
end
