% 更多结构体、元胞、句柄的组合测试和多个出参的匿名句柄测试
function test13_3
    aa = struct();
    aa.age = 30;

    aa.cell{1} = struct('func', @sin, 'value', pi/2);
    aa.cell{2} = struct('func', @cos, 'value', 0);
    result1 = aa.cell{1}.func(aa.cell{1}.value);
    result2 = aa.cell{2}.func(aa.cell{2}.value);
    
    if abs(result1 - 1) > 1e-6 || abs(result2 - 1) > 1e-6
        error('1');
    end
    
    aa.cell_ops{1} = {1, 2, 3};
    aa.cell_ops{2} = struct('a', 1, 'b', 2);
    aa.cell_ops{3} = {@() disp('Test'), @() 42};
    
    aa.cell_ops{3}{1}();
    if aa.cell_ops{3}{2}() ~= 42
        error('2');
    end

    aa.anon_condition = @() struct('result', aa.age > 25);
    aa.anon_result = aa.anon_condition().result;
    
    if aa.anon_result ~= true
        error('3');
    end
    
    aa.c.struct_field = struct('a', 1, 'b', 2);
    aa.c.cell_field{1} = struct('c', 3, 'd', 4);
    aa.c.cell_field{2} = @() aa.c.struct_field.a + aa.c.struct_field.b;
    c_result = aa.c.cell_field{2}();
    
    if c_result ~= 3
        error('4');
    end
    
    fh = @()f1();
    [c,d] = fh();
    if c~=1 || d~=2
        error('5')
    end
    [a,b,c,d,e] = fh();
    if a~=1 || b~=2 || c~=3 || d~=4 || e~=5
        error('6')
    end
    
    fh1 = @() deal([1, 2], [3, 4]);
    [a, b] = fh1();
    if a(1)~=1 || a(2)~=2 || b(1)~=3 || b(2)~=4
        error('7')
    end
    
    fh2 = @(x, y) deal(x + y, x - y, x * y, x / y);
    [a,b,c,d] = fh2(10, 5);
    if a~=15 || b~=5 || c~=50 || d~=2
        error('8')
    end
    
    fh3 = @(c, inds)c{inds};
    c3 = {1,2,3,4};
    [a, b] = fh3(c3, 2:3); % 天元暂时有 bug，按 Matlab 生成
    if a ~= 2 || b ~= 3
        error('9');
    end

    fh4 = @(x, y) deal(x, y);
    [a, b] = fh4(1, 2);
    if a ~= 1 || b ~= 2
        error('10');
    end

    % 测试 set_doub() 非 bex 原型
    fh5 = @(x) 3.14;
    a = fh5(0);
    if a ~= 3.14
        error('11');
    end
    

end

function [a,b,c,d,e] = f1()
    a = 1;
    b = 2;
    c = 3;
    d = 4;
    e = 5;
end


