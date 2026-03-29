% 结构体
function test13
    % 使用struct函数创建结构体
    person = struct('name', 'Alice', 'age', 30, 'height', 1.75);
    % node_struct_set,放入char, double , cell,logic
    person.name = 'Alice';
    person.age = 25;
    person.cell = {1, 2};
    person.logic = (1==2 || 2==2) && (1 ~= 2);
    disp(person);
    % 实现将结构体的值赋值给其他变量
    a = person.name;
    disp(a)
    % 结构体嵌套
    b.name.age.happy = 15;
    b.name.age.happy = 16;
    if b.name.age.happy ~= 16
        error('1')
    end
    aa.b.c(1,2) = 3;
    if aa.b.c(1,2) ~= 3
        error('2')
    end
    disp(b)
    c = b.name.age;
    disp(c)
    % 结构体的值作为参数传递
    d = jiafa(1, b.name.age.happy);
    disp(d)
    % 合并两个结构体
    person1 = struct('name', 'Alice', 'age', 30);
    person2 = struct('name', 'Charlie', 'age', 28);
    mergeS = [person1,person2];
    disp(mergeS(1).name);
    
    aa.b = sin(1); % struct_set(name, name, multiple_func)
    [aaa.c, aaa.d] = get12(1, 2); % multiple_func(horz_list(struct_get,struct_get), ...)
    if aa.b ~= sin(1) || aaa.c ~= 1 || aaa.d ~= 2
        error('3');
    end

    % TODO
    % mycell = {1, 2};
    % [cc.a, cc.b] = mycell{:}; % asgn(horz_list(struct_get,struct_get), cell_get())
    % if cc.a ~= 1 || cc.b ~= 2
    %     error('3');
    % end

    % 复杂 setter 嵌套
    bb = {1, struct('abc', 1, 'def', 2)};
    ind = 2;
    bb{ind}.def = cell(1,3);
    bb{ind}.def{round(pi)} = ['3' num2str(2)]; % cell_set{struct_get[cell_get(bb, ind), def], multiple_func(round, pi), horz_list}
    if ~strcmp(bb{ind}.def{round(pi)}, '32')
        error('3.5')
    end

    c = {1, 2; 3, 4};
    b.c = zeros(2, 3);
    b.d = cell(3, 1);

    % b.c(round(sin(1)), :) 表示为
    % multiple_func(,struct_get(b,c),list(multiple_func(,round,sin),magic_colon))
    % b.d{end} 表示为
    % cell_get(struct_get(b,d), list(magic_end))
    % [a, b.c(round(sin(1)), :), b.d{end}] = my_func(@(x)sin(x).^2, c{2:4})
    [a, b.c(round(sin(1)), 2), b.d{3}] = my_func(@(x)sin(x).^2, c{2:4});
    if a ~= 1
        error('4')
    end
    if any(b.c(round(sin(1)), 2) ~= 2)
        error('5')
    end
    if b.d{end} ~= 3
        error('6')
    end
    % struct_set(struct_get(b,d), e, 6)
    b.e.f = 6;
    if b.e.f ~= 6
        error('7')
    end
end

function output = jiafa(input1, input2)
    output = input1 + input2;
end

function [a,b] = get12(input1,input2)
    a = 1;
    b = 2;
end

function [c,d,e] = my_func(a1, a2, a3, a4)
    c = 1;
    d = 2;
    e = 3;
end
