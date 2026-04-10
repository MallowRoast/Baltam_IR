function test33_4
    test_builtin_abs();
    test_builtin_asin();
    test_builtin_ceil();
    test_builtin_char();
    test_builtin_cos();
    test_builtin_dot();
    test_builtin_find();
    test_builtin_int16();
    test_builtin_isempty();
    test_builtin_isscalar();
    test_builtin_isvector();
    test_builtin_magitest_builtin_end();
    test_builtin_ndims();
    test_builtin_ones();
    test_builtin_op_colon();
    test_builtin_op_elm_get();
    test_builtin_op_elm_set();
    test_builtin_rand();
    test_builtin_randi();
    test_builtin_rem();
    test_builtin_reshape();
    test_builtin_sin();
    test_builtin_size();
    test_builtin_strcmp();
    test_builtin_uint16();
    test_builtin_zeros();
    test_internal_switch();
end

function test_builtin_abs
    % 测试 double 标量
    a = abs(-1.2) - 1.2;
    if a > 1e-10
        error('a 的值不为1.2');
    end
    % 类型适配分支测试内容
    a = abs(int8([1,2.1,3]));
    if a ~= int8([1,2,3])
        error('a 的值不为[1,2,3]');
    end
    disp('abs 运行成功');
end

function test_builtin_asin
    % 标量
    a = asin(0.5) - 0.5236;
    if a > 1e-10
        error('1');
    end
    % 向量
    b = asin([-1, -0.5, 0, 0.5, 1]);
    c = [-1.5708   -0.5236    0.0000    0.5236    1.5708];
    for i1 = 1:5
        i = i1(1);
        if b(i) - c(i) > 1e-3
            error('2');
        end
    end
end

function test_builtin_ceil
    % double 标量
    a = ceil(1.2);
    if a - 2 > 1e-10
        error('ceil 出错');
    end

    % 类型适配
    a = ceil(int8(4));
    if double(a) - 4 > 1e-10
        error('ceil 出错');
    end
    b = ceil(single(3.1));
    if double(b) - 4 > 1e-10
        error('ceil 出错');
    end
    disp('ceil 运行成功');
end

function test_builtin_char
   a = char(77);
   b = char([77 65 84 76 65 66]);
   if ~strcmp(a,'M')
        error('a 不为字符 M');
   end
   if ~strcmp(b,'MATLAB')
        error('b 不为字符向量 MATLAB');
   end
   disp('char 运行成功');
end

function test_builtin_cos
    % 标量
    a = cos(2);
    if a + 0.4161 > 1e-4
        error('cos 函数出错');
    end
    % 向量
    b = cos([1,2,3]);
    c = [0.5403   -0.4161   -0.9900];
    for i1 = 1:3
        i = i1(1);
        if b(i) - c(i) > 1e-4
            error('cos 函数出错');
        end
    end
    disp('cos 函数运行成功');
end

function test_builtin_dot
    a = [1,2,3;3,4,5];
    b = [4,5,6;6,7,8];
    c = dot(a,b);
    c1 = [22,38,58];
    for i1 = 1:3
        i = i1(1);
        if c(1) - c1(1) > 1e-4
            error('dot 函数出错');
        end
    end
    d = [1,2,3];
    e = [4,5,6];
    f = dot(d,e);
    if f(1) - 32 > 1e-4
        error('dot 函数出错');
    end
    disp('dot 函数运行成功');
    %应该报错
    % a = [1,2;3,4;5,6];
    % b = [1,2,3;4,5,6];
    % c = dot(a,b);
end

function test_builtin_find
    % double 矩阵
    a = [1,0,2;3,4,0];
    b = find(a);
    b_ret = [1,2,4,5];
    for i1 = 1:4
        i = i1(1);
        if b(1) - b_ret(1) > 1e-4
            error('find 函数出错');
        end
    end

    % 类型适配测试的内容
    X = int8([2, 8, 0; 0, 3, 9; 0, 0, 1]);
    a = find(X);
    a_ret = [1,4,5,8,9];
    for i1 = 1:5
        i = i1(1);
        if a(i) - a_ret(i) > 1e-4
            error('find 函数出错');
        end
    end
    disp('find 函数运行成功');
end

function test_builtin_int16
    a = int16(100.6);
    if double(a) - 101 > 1e-4
        error('int16函数出错')
    end
    b = int16([2.2,3.1,4.5]);
    b1 = [2,3,5];
    for i1 = 1:3
        i = i1(1);
        if double(b(i)) - b1(i) > 1e-4
            error('int16 函数出错');
        end
    end
    disp('int16 函数运行成功');
end

function test_builtin_isempty
    a = zeros(2,0,3);
    b = isempty(a);
    if ~b
        error('isempty 出错')
    end
    disp('isempty 运行成功');
end

function test_builtin_isscalar
    a = [1,2,3];
    % 矩阵或向量入参
    b = isscalar(a);
    if b
        error('isscalar 运行错误');
    end
    % double 入参
    c = isscalar(1);
    if ~c
        error('isscalar 运行错误');
    end
    disp('isscalar 运行成功');
end

function test_builtin_isvector
    a = [1,2,3];
    b = isvector(a);
    if ~a
        error('isvector 出错');
    end
    disp('isvector 运行成功');
end

function test_builtin_magitest_builtin_end
    a = [1,2,3;3,4,5];
    b = a(end);
    if b - 5 > 1e-4
        error('magic_end 出错');
    end
    c = a(1,end);
    if c - 5 > 1e-4
        error('magic_end 出错')
    end
    disp('magic_end 运行成功');
end

function test_builtin_ndims
    a = zeros(2,3,4);
    b = ndims(a);
    if b - 3 > 1e-4
        error('ndims 运行出错');
    end

    % 适配其他类型
    c = int16([2,3;3,2]);
    if ndims(c) ~= int16(2)
        error('ndims 运行出错')
    end
    disp('ndims 运行成功');
end

function test_builtin_ones
    % 入参全为整数
    a = ones(2,3,1,1);
    if a ~= [1,1,1;1,1,1]
        error('ones 出错');
    end
    % 入参为空
    b = ones();
    if b - 1 > 1e-4
        error('ones 出错');
    end
    % 入参为向量
    c = ones([3,2,1,1]);
    if c ~= [1,1;1,1;1,1]
        error('ones 出错');
    end
    disp('ones 运行成功');
end

function test_builtin_op_colon
    a = 2:3;
    if a ~= [2,3]
        error('op_colon 运行出错');
    end
    b = 3.2:0.5:7.5;
    if b ~= [3.2,3.7,4.2,4.7,5.2,5.7,6.2,6.7,7.2]
        error('op_colon 运行出错');
    end
    % 类型适配测试的内容
    b = 2:1:int16(5.2);
    if b ~= int16([2,3,4,5]);
        error('op_colon 运行出错');
    end
    c = 3:int8(5.3);
    if c ~= int8([3,4,5])
        error('op_colon 运行出错');
    end
    disp('op_colon 运行成功');
end

function test_builtin_op_elm_get
    % 返回矩阵
    a = [1,2,3,4,5,6;7,8,9,10,11,12;13,14,15,16,17,18;19,20,21,22,23,24];
    b = a(:,2);
    if b ~= [2;8;14;20]
        error('op_elm_get 出错');
    end
    c = a([1,2],3);
    if c ~= [3;9]
        error('op_elm_get 出错');
    end
    a = ones(3, 2, 4, 7);
    e = a(2,[10,11]);
    if e ~= [1,1]
        error('op_elm_get 出错');
    end

    % 返回标量
    a = [1,2,3,4,5,6;7,8,9,10,11,12;13,14,15,16,17,18;19,20,21,22,23,24];
    d = a(2,6);
    if d - 12 > 1e-4
        error('op_elm_get 出错');
    end
    d = a(24);
    if d -24 > 1e-4
        error('op_elm_get 出错');
    end
    disp('op_elm_get 运行成功');
end

function test_builtin_op_elm_set
    % 入参为标量double
    a = zeros(3);
    a(1,1) = 1;
    if a~= [1,0,0;0,0,0;0,0,0]
        error('op_elm_set 运行出错');
    end
    % 等号右边为矩阵
    a(1,2) = [2]
    if a ~= [1,2,0;0,0,0;0,0,0]
        error('op_elm_set 运行出错');
    end
    % 一维索引
    a(3) = 3
    if a ~= [1,2,3;0,0,0;0,0,0]
        error('op_elm_set 运行出错');
    end
    % 入参包含冒号,等号右边为多元素向量
    a(3,:) = [4,5,6]
    if a ~= [1,2,3;0,0,0;4,5,6]
        error('op_elm_set 运行出错');
    end
    % 自动扩充矩阵(未实现)
    % a(4,4) = 4
    % if a ~= [1,2,3;0,0,0;0,0,4]
        % error('op_elm_set 运行出错');
    % end
    % 入参为向量
    b = zeros(3);
    b([1,2],[1,2]) = 1;
    if b ~= [1,1,0;1,1,0;0,0,0]
        error('op_elm_set 运行出错');
    end      
    disp('op_elm_set 运行成功');
end

function test_builtin_rand
    % 入参为标量
    a = rand(3);
    if size(a) ~= [3,3]
        error('rand 函数出错');
    end
    disp('rand(3):');
    disp(a)
    % 入参为多个double
    b = rand(3,1,2);
    if size(b) ~= [3,1,2]
        error('rand 函数出错');
    end
    disp('rand(3,1,2):')
    disp(size(b));
    % 维度压缩
    c = rand(3,1,1);
    if size(c) ~= [3,1]
        error('rand 函数出错');
    end
    disp('rand(3,1,1):');
    disp(c);
    % 入参全为1
    d = rand(1,1,1,1,1);
    if size(d) ~= [1,1]
        error('rand 函数出错');
    end
    disp('rand(1,1,1,1,1):');
    disp(d);
    % 入参含有负数
    e = rand(-1,1,1);
    if size(e) ~= [0,1]
        error('rand 函数出错');
    end
    % 正常入参
    f = rand(3,1);
    if size(f) ~= [3,1]
        error('rand 函数出错');
    end
    disp('rand(3,1):');
    disp(f);
    % 入参为数组
    g = rand([3,1,1]);
    if size(g) ~= [3,1]
        error('rand 函数出错');
    end
    disp('rand([3,1,1]):');
    disp(g);
    % 单个负数入参
    h = rand(-1);
    if size(h) ~= [0,0]
        error('rand 函数出错');
    end
    % 入参为数组，检测是否进行维度压缩
    j = rand([3,1,1,1,1]);
    if size(j) ~= [3,1]
        error('rand 函数出错');
    end
    disp('rand([3,1,1,1,1]):');
    disp(j);
    % 元素全为 1 的数组
    i= rand([1,1,1,1,1]);
    if size(i) ~= [1,1]
        error('rand 函数出错');
    end
    disp('rand([1,1,1,1,1]):');
    disp(i);
    % 入参为空
    k = rand();
    if ~isscalar(k)
        error('rand 函数出错');
    end
    disp('rand 函数运行结束');

    %% 应该报错的部分
    % a = rand(2.1,1);
    % disp(a);
    % c = randi([1.2,3.1]);
    % disp(c);
end

function test_builtin_randi
    % a = randi(5,3);
    % disp(a);
    % 正常入参
    b = randi(5,3,1,2);
    if size(b) ~= [3,1,2]
        error('randi 函数出错');
    end
    % double 入参，检测维度是否压缩
    c = randi(5,3,1,1);
    if size(c) ~= [3,1]
        error('randi 函数出错');
    end
    disp('randi(5,3,1,1):')
    disp(c);
    % 维度入参全为1，检测维度是否正确
    d = randi(5,1,1,1,1,1);
    if size(d) ~= [1,1]
        error('randi 函数出错');
    end
    disp('randi(5,1,1,1,1,1):')
    disp(d);
    % 维度含有负数
    e = randi(5,-1,1,1);
    if size(e) ~= [0,1]
        error('randi 函数出错');
    end
    % 数组指定随机数范围
    f = randi([2,3],3,1);
    disp('randi([2,3],3,1)');
    disp(f);
    % 数组指定随机数范围和维度
    g = randi([2,4],[3,1,1]);
    if size(g) ~= [3,1]
        error('randi 函数出错');
    end
    disp('randi([2,4],[3,1,1])');
    disp(g);
    % 维度单负数
    h = randi(5,-1);
    if size(h) ~= [0,0]
        error('randi 函数出错');
    end
    % 入参都为数组，压缩维度
    j = randi([2,4],[3,1,1,1,1]);
    if size(j) ~= [3,1]
        error('randi 函数出错');
    end
    disp(' randi([2,4],[3,1,1,1,1])');
    disp(j);
    % 入参为数组，维度的元素全为1
    i= randi([2,4],[1,1,1,1,1]);
    if size(i) ~= [1,1]
        error('randi 函数出错');
    end
    disp('randi([2,4],[1,1,1,1,1])');
    disp(i);
    % 单入参
    k= randi(3);
    disp('randi(3)');
    disp(k);
    % 数组指定随机数范围，维度为空
    l= randi([2,3]);
    if ~isscalar(l)
        error('randi函数出错');
    end
    disp('randi([2,3])');
    disp(l);
    % 维度单入参
    m = randi([2,3],[2]);
    if size(m) ~= [2,2]
        error('randi 函数出错');
    end
    disp('randi([2,3],[2])');
    disp(m);
    n = randi(3,[3]);
    disp('randi(3,[3])');
    disp(n);
    disp('randi 函数运行结束');
    %% 应该报错的部分
    % a = randi(0,1,2);
    % disp(a);
    % b = randi([3,2],2,1);
    % disp(b);
    % c = randi(3,[1.2,3.1]);
    % disp(c);
    % d = randi([2.3],2.1,3.1);
    % disp(d);
    % e = randi(3,2.1,3.2);
    % disp(e);
end

function test_builtin_rem
    %测试 double 类型的输入
    a = rem(3.2, 2);
    if a - 1.2 > 1e-4
        error('rem 运行出错');
    end

    % 类型合并分支测试内容
    a = rem(int8(5),2);
    if a ~= int8(1)
        error('rem 运行出错');
    end
    b = rem(int16(5.1),3);
    if b ~= int16(2)
        error('rem 运行出错');
    end
    disp('rem 运行成功');
end

function test_builtin_reshape
    a = 1:12;
    a = reshape(a, 3,4,1); %修改自身
    if a ~= [1,4,7,10;2,5,8,11;3,6,9,12]
        error('reshape 运行出错');
    end
    b = reshape(a,3,4,1); % 自动压缩维度
    if b ~= [1,4,7,10;2,5,8,11;3,6,9,12]
        error('reshape 运行出错');
    end
    c = reshape(a, [3,4]);
    if c ~= [1,4,7,10;2,5,8,11;3,6,9,12]
        error('reshape 运行出错');
    end
    d = reshape(a, 3,4,[],1); %维度压缩
    if d ~= [1,4,7,10;2,5,8,11;3,6,9,12]
        error('reshape 运行出错');
    end
    e = reshape(a, [12]); %单元素向量
    if e ~= [1;2;3;4;5;6;7;8;9;10;11;12]
        error('reshape 运行出错');
    end
    f = reshape(a, [],4,1,1);% 第一个位置是空矩阵
    if f ~= [1,4,7,10;2,5,8,11;3,6,9,12]
        error('reshape 运行出错');
    end
    g = reshape(a, ,2, 6,); % 测试空
    if g ~= [1,3,5,7,9,11;2,4,6,8,10,12]
        error('reshape 运行出错');
    end
    h = reshape(a,12);
    if h ~= [1;2;3;4;5;6;7;8;9;10;11;12]
        error('reshape 运行出错');
    end
    disp('reshape 运行成功');

    %% 应该报错
    % a = 1:12
    % b = reshape(a,[3,4,-1]);
    % disp(b);
    % c = reshape(a,[3,1,2.4]);
    % disp(c);
    % d = reshape(a,2,1,3.1);
    % disp(d);
end

function test_builtin_sin
     % 标量
     a = sin(1);
     if a - 0.8415 > 1e-3
         error('sin 运行错误');
     end
     % 向量
     b = sin([1,2,3]);
     b1 = [0.8415, 0.9093, 0.1411];
     for i1 = 1:3
         i = i1(1);
         if abs(b(i) - b1(i)) > 1e-3
             error('sin 运行错误');
         end
     end
     disp('sin 运行成功');
 end

 function test_builtin_size
     a = zeros(2,3,4);
     if size(a) ~= [2,3,4]
         error('size 函数出错');
     end
     if size(a,2) ~= 3
         error('size 函数出错');
     end

     % 适配其他类型矩阵
     b = int8([2,3]);
     if size(b) ~= [1, 2]
         error('size 函数出错');
     end
     if size(b,2) ~= 2
         error('size 函数出错');
     end
     disp('size 函数运行成功');
 end

 function test_builtin_strcmp
    % 字符数组
    a = strcmp(['aa';'aa';'aa'],['aa';'aa';'aa']);
    if ~a
        error('strcmp 函数出错');
    end
    b = strcmp(['ab';'cd';'ef'],['ace';'bdf']);
    if b
        error('strcmp 函数出错');
    end
    % 字符向量
    c = strcmp('asd','bsd');
    if c
        error('strcmp 函数出错');
    end
    disp('strcmp 运行成功');
 end

 function test_builtin_uint16
    a = uint16(100.4);
    if a ~= uint16(100)
        error('uint16 运行出错');
    end
    b = uint16([3,2.2,4]);
    if b ~= uint16([3,2,4])
        error('uint16 运行出错')
    end
    disp('uint16 运行成功');
 end

 function test_builtin_zeros
    % 入参全为整数
    a = zeros(2,3,1,1);
    if a ~= [0,0,0;0,0,0]
        error('zeros 函数出错');
    end
    % 入参为空，这个时候返回标量
    b = zeros();
    if b ~= 0
        error('zeros 函数出错');
    end
    % 入参为向量
    c = zeros([3,2,1,1]);
    if c ~= [0,0;0,0;0,0]
        error('zeros 函数出错');
    end
    disp('zeros 函数运行成功');
 end

 function test_internal_switch
    % TODO 目前没有otherwise 会导致phi 出错
    a = 1;
    % 数值
    switch(a)
        case 1
            disp('switch匹配数值成功');
        case 2
            error('swich 出错');
        otherwise
            error('swich 出错');
    end

    b = 'asd';
    % 字符向量
    switch(b)
        case 'asd'
            disp('switch 字符向量匹配成功');
        case 'dasg'
            error('swich 出错');
        otherwise
            error('swich 出错');
    end
    disp('switch 运行成功');
end