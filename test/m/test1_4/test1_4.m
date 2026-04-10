% if 条件类型自动转换
function test1_4
a = [1, 2, 3];
if a
    disp('[1, 2, 3] 是 true');
else
    disp('[1, 2, 3] 是 false');
end
a = [1, 2, 0];
if a
    disp('[1, 2, 0] 是 true');
else
    disp('[1, 2, 0] 是 false');
end
a = [];
a = int64(a);
if a
    disp('[] 是 true');
else
    disp('[] 是 false');
end
a = 'Hello';
if a
    disp('"Hello" 是 true');
else
    disp('"Hello" 是 false');
end
a = '';
if a
    disp(''' 是 true');
else
    disp(''' 是 false');
end

a = [1, 2; 3, 4];
if a
    disp('[1, 2; 3, 4] 是 true');
else
    disp('[1, 2; 3, 4] 是 false');
end
a = int64(a);
if a
    disp('int64的[1, 2; 3, 4] 是 true');
else
    disp('int64的[1, 2; 3, 4] 是 false');
end
a = [1, 0; 3, 4];
if a
    disp('[1, 0; 3, 4] 是 true');
else
    disp('[1, 0; 3, 4] 是 false');
end
a = int64(a);
if a
    disp('int64的[1, 0; 3, 4] 是 true');
else
    disp('int64的[1, 0; 3, 4] 是 false');
end

a = randi([1, 1], 2, 2, 3, 4);
if a
    disp('没有 0 的高维矩阵 是 true');
else
    disp('没有 0 的高维矩阵 是 false');
end

a = randi([0, 1], 2, 2, 3, 4);
if a
    disp('有 0 的高维矩阵 是 true');
else
    disp('有 0 的高维矩阵 是 false');
end

% 类型为 cell, struct, "", 句柄等都不能转换为 logical 类型
end
