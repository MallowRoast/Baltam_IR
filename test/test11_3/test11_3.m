% 出参中的元胞解包
% matlab 可以正常运行，但天元还有 bug，按 matlab 的结果生成
function test11_3
    c1 = cell(4,4);
    c3 = cell(4,4);
    c4 = cell(10,10);
    for i = 1:10
        for j = 1:10
            c4{i, j} = i * j * rand(1,1); 
        end
    end
    % 以下代码在 matlab 能正常运行，北太不行
    [cc.a, cc.b] = c4{:};
    if cc.a ~= c4{1,1} || cc.b ~= c4{2,1}
        error('1')
    end

    [a.b{2}.d, c1{2:3}, d, c2.b, c3{2:3}] = deal(1,2,3,4,5,6,7);
    if a.b{2}.d ~= 1 || c1{2} ~= 2 || c1{3} ~= 3 || d ~= 4 || c2.b ~= 5 || c3{2} ~= 6 || c3{3} ~= 7
        error('1.5');
    end

    % c1{:} = deal(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16); % 应该报错

    [c1{:}] = deal(1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16);
    if c1{1} ~= 1 || c1{2} ~= 2 || c1{3} ~= 3 || c1{4} ~= 4 || ...
       c1{5} ~= 5 || c1{6} ~= 6 || c1{7} ~= 7 || c1{8} ~= 8 || ...
       c1{9} ~= 9 || c1{10} ~= 10 || c1{11} ~= 11 || c1{12} ~= 12 || ...
       c1{13} ~= 13 || c1{14} ~= 14 || c1{15} ~= 15 || c1{16} ~= 16
        error('1.8');
    end

    [a.bb.c{1:2}] = f1();
    if a.bb.c{1} ~= 1 || a.bb.c{2} ~= 2
        error('2')
    end
    
    dd.e.f = cell(3,2);
    [a.bb.c{1:2}, dd.e.f{[1,3], :}, e, c3{3:4, [1,3]}] = c4{1:11};
    if a.bb.c{1} ~= c4{1} || a.bb.c{2} ~= c4{2} || dd.e.f{1,1} ~= c4{3} || dd.e.f{3,1} ~= c4{4} || e ~= c4{7} || c3{3,1} ~= c4{8} || c3{3,3} ~= c4{10}
        error('3');
    end
    
    [c1{2,3}, c3{2:3}, b.c(round(sin(1)), 2)] = f1();
    if c1{2,3} ~= 1 || c3{2} ~= 2 || c3{3} ~= 3 || b.c(round(sin(1)), 2) ~= 4
        error('4');
    end
    
    [a, b.c(round(sin(1)), 2), b.d{3}] = c4{2:4};
    if a ~= c4{2} || b.c(round(sin(1)), 2) ~= c4{3} || b.d{3} ~= c4{4}
        error('5');
    end
    
    [a, b.c(round(sin(1)), 2), b.d{3}] = my_func(@(x) sin(x).^2, c1{2:4});
    if a ~= 1 || b.c(round(sin(1)), 2) ~= 2 || b.d{3} ~= 3
        error('6');
    end
    
    c1{2,3} = f1();
    if c1{2,3} ~= 1
        error('7');
    end
    
    [c1{2:3}] = f1();
    if c1{2} ~= 1 || c1{3} ~= 2
        error('8');
    end
    
    [c1{2:3}, d, c2.b]  = f1();
    if c1{2} ~= 1 || c1{3} ~= 2 || d ~= 3 || c2.b ~= 4
        error('9');
    end
    
    [a, c1{2:3}, c] = f1();
     if a ~= 1 || c1{2} ~= 2 || c1{3} ~= 3 || c ~= 4
        error('10');
     end
     
    [a, c1{2,3}, c, b] = f1();
    if a ~= 1 || c1{2,3} ~= 2 || c ~= 3 || b ~= 4
        error('11');
    end

    [c1{2}, ca{1}, ca{2:3}] = c4{1:4};
    if c1{2} ~= c4{1} || ca{1} ~= c4{2} || ca{2} ~= c4{3} || ca{3} ~= c4{4}
        error('12');
    end
        
    ind = 2:3;
    ind2 = [1,4];
    [c1{ind}, c3{ind2}] = f1();
    if c1{2} ~= 1 || c1{3} ~= 2 || c3{1} ~= 3 || c3{4} ~= 4
        error('13');
    end

    mycell = cell(3,3,3);
    mycell{1,2,3} = c4{:};
    if mycell{1,2,3} ~= c4{1}
        error('14');
    end

    [mycell{1,2,3}] = c4{2:3};
    if mycell{1,2,3} ~= c4{2}
        error('15');
    end
        
    [mycell{2,2:3,6}] = f1();
    if mycell{2,2,6} ~= 1 || mycell{2,3,6} ~= 2
        error('15')
    end
        
    ind3 = [7,9];
    [mycell{1,2,3}, mycell{1,3:4,5}, mycell{ind3}] = c4{3:end};
    if mycell{1,2,3} ~= c4{3} || mycell{1,3,5} ~= c4{4} || mycell{1,4,5} ~= c4{5} || mycell{7} ~= c4{6} || mycell{9} ~= c4{7}
        error('16');
    end

    disp("测试通过");
  
end

function [e, f, g, h] = f1
if nargout >= 1
    e = 1;
    if nargout >= 2
        f = 2;
        if nargout >= 3
            g = 3;
            if nargout >= 4
                h = 4;
                if nargout > 4
                    error('0');
                end
            end
        end
    end
end
end

function [c,d,e] = my_func(a1, a2, a3, a4)
    c = 1;
    d = 2;
    e = 3;
end
