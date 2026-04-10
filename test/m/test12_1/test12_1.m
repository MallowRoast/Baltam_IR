% feval 函数
function test12_1
    a = feval(@(x)x+3,2);
    if a ~= 5
        error('1');
    end
    
    b = feval('sin',0);
    if b ~= sin(0)
        error('2');
    end
    
    c = feval('a1',1);
    if c ~= 1
        error('3');
    end
    
    fh = @(x)x+1;
    
    % 该语句北太能跑，matlab不能跑
    %d = feval('fh',1);
    
    d = feval(fh,1);
    if d ~= 2
        error('4');
    end
    
    [e,f] = feval('a2', 1, 2);
    if e ~= 1 || f ~= 2
        error('5');
    end
    
    % 测试 op_placeholder 结点
    fh = @(x,~, y,~) x + 1 + y;
    ff = fh(3,2,4);
    if ff ~= 8
       error('6');
    end
    [aa,~,bb,~] = f1(1,2,3,4,5);
    if aa ~= 1 || bb ~= 5
       error('7');
    end
end

function a = a1(x)
    a = x;
end

function [a,b] = a2(x,y)
    a = x;
    b = y;
end

function [a,b,c,d] = f1(q,~,w,~,e,~)
    a =q;
    b =w;
    c =e;
    d =1; 
end

