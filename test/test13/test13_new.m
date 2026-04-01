function test13
    % 使用struct函数创建结构体
    person = struct('name', 'Alice', 'age', 30, 'height', 1.75);
    disp(person);
    % node_struct_set,放入char, double , cell,logic
    person.name = 'Alice';
    person.age = 25; % 改变属性
    person.cell = {1,2}; % 新增属性
    person.logic = ((1==2)||(2==2))&&(1 != 2); % 新增属性
    disp(person);
    % 实现将结构体的值赋值给其他变量
    a = person.name;
    disp(a)
    % 结构体镶嵌结构体
    b.name.age.happy=15;
    disp(b)
    c = b.name.age;
    disp(c)
    % 结构体的值作为参数传递
    d = jiafa(1, b.name.age.happy);
    disp(d)
    % 结构体作为参数传递
    e = f2(person, b.name)
    disp(e);
    % 合并两个结构体
    person1 = struct('name', 'Alice', 'age', 30);
    person2 = struct('name', 'Charlie', 'age', 28);
    mergeS = [person1, person2];
    disp(mergeS(1).name);
end

function output = jiafa(input1,input2)
    output = input1 + input2;
end

function st = f2(st1, st2)
    disp(st1);
    disp(st2);
    st = struct('a', 1, 'b', '2');
end
