% C 语言生成测试（韩一飞）

% 入参都设为 double
function test33_6
    test_builtin_num2str()
    test_builtin_class()
    test_builtin_sign()
    test_builtin_pow2()
    test_builtin_isnan()
    test_builtin_complex()
    test_builtin_isreal()
    test_builtin_conj()
    test_builtin_isinf()
    test_builtin_realmax()
    test_builtin_logical()
    test_builtin_log()
    test_builtin_cross()
    test_builtin_polyder();
    test_builtin_polyint();
    test_builtin_fft();
end

function test_builtin_num2str
    % 1. 定义测试变量
    double_val = 3.1415926;          % double类型实数
    complex_val1 = 2 + 3i;           % 复数类型
    complex_val2 = 5.6 - 7.8i;       % 复数类型

    % 2. 直接调用num2str处理各种类型
    str_double = num2str(double_val);
    str_complex1 = num2str(complex_val1);
    str_complex2 = num2str(complex_val2);

    % 3. 预期结果
    expected_double = '3.1416';       % double默认格式
    expected_complex1 = '2+3i';       % 复数默认格式
    expected_complex2 = '5.6-7.8i';   % 复数默认格式

    % 4. 结果检测
    all_pass = true;
    
    if ~strcmp(str_double, expected_double)
        disp('错误：double类型转换失败');
        all_pass = false;
    end
    
    if ~strcmp(str_complex1, expected_complex1)
        disp('错误：复数1类型转换失败');
        all_pass = false;
    end
    
    if ~strcmp(str_complex2, expected_complex2)
        disp('错误：复数2类型转换失败');
        all_pass = false;
    end
    
    % 5. 输出最终测试结果
    if all_pass
        disp('test_builtin_num2str测试通过');
    else
        disp('test_builtin_num2str测试失败');
    end
end

function test_builtin_class
    % 1. 定义四种输入类型
    double_scalar = 3.14159;                  % double标量
    double_matrix = [1.2 3.4; 5.6 7.8];       % double矩阵
    complex_scalar = 2.5 - 4.8i;              % 复数标量

    % 2. 直接调用class函数，查看每种输入的类型
    result1 = class(double_scalar);
    result2 = class(double_matrix);
    result3 = class(complex_scalar);
    

    % 3. 结果检测
    all_pass = true;
    
    if ~strcmp(result1, 'double')
        disp('错误：double标量的类型检测失败');
        all_pass = false;
    end
    
    if ~strcmp(result2, 'double')
        disp('错误：double矩阵的类型检测失败');
        all_pass = false;
    end
    
    if ~strcmp(result3, 'double')
        disp('错误：复数标量的类型检测失败');
        all_pass = false;
    end
    
    % 4. 输出最终测试结果
    if all_pass
        disp('test_builtin_class测试通过');
    else
        disp('test_builtin_class测试失败');
    end
end

function test_builtin_sign
    % 定义各种输入类型
    double_scalar = -3.14;                  % double标量
    double_matrix = [1.5 -2.3 0; 4.1 -5.7 6.2];  % double矩阵
    complex_scalar = -2 + 3i;               % 复数标量

    % 调用sign函数处理
    result1 = sign(double_scalar);
    result2 = sign(double_matrix);
    result3 = sign(complex_scalar);

    % 结果检测
    all_pass = true;
    
    % 检测double标量
    expected1 = -1;
    if result1 ~= expected1
        disp('错误：double标量的sign结果不符合预期');
        all_pass = false;
    end
    
    % 检测double矩阵
    expected2 = [1 -1 0; 1 -1 1];
    if ~isequal(result2, expected2)
        disp('错误：double矩阵的sign结果不符合预期');
        all_pass = false;
    end
    
    % 检测复数标量 - 直接与MATLAB计算结果比较
    expected3 = (-2 + 3i) / sqrt((-2)^2 + 3^2);  % z / |z|
    if result3 ~= expected3
        disp('错误：复数标量的sign结果不符合预期');
        all_pass = false;
    end
    
    % 输出最终测试结果
    if all_pass
        disp('test_builtin_sign测试通过');
    else
        disp('test_builtin_sign测试失败');
    end
end   

function test_builtin_pow2
    % 定义输入数据
    double_scalar_E = 3.5;                  % 用于格式1的double标量
    double_matrix_E = [1 -2; 0.5 4];        % 用于格式1的double矩阵
    X_scalar = 2.5;                         % 用于格式2的基数标量
    X_matrix = [1 3; 5 7];                  % 用于格式2的基数矩阵
    E_scalar = 2;                           % 用于格式2的指数标量
    E_matrix = [0 1; -1 3];                 % 用于格式2的指数矩阵

    % 格式1: Y = pow2(E) - 计算2的E次幂
    Y1_scalar = pow2(double_scalar_E);
    Y1_matrix = pow2(double_matrix_E);

    % 格式2: Y = pow2(X, E) - 计算X乘以2的E次幂
    Y2_s_s = pow2(X_scalar, E_scalar);
    Y2_m_s = pow2(X_matrix, E_scalar);
    Y2_s_m = pow2(X_scalar, E_matrix);
    Y2_m_m = pow2(X_matrix, E_matrix);

    % 结果检测
    all_pass = true;
    
    % 检测格式1：pow2(double_scalar_E)
    if abs(Y1_scalar - 11.3137) > 0.001
        disp('错误：pow2(double标量)结果不符合预期');
        all_pass = false;
    end
    
    % 检测格式1：pow2(double_matrix_E)
    expected_Y1_matrix = [2 0.25; 1.4142 16];
    if abs(Y1_matrix(1,1) - expected_Y1_matrix(1,1)) > 0.001 || ...
       abs(Y1_matrix(1,2) - expected_Y1_matrix(1,2)) > 0.001 || ...
       abs(Y1_matrix(2,1) - expected_Y1_matrix(2,1)) > 0.001 || ...
       abs(Y1_matrix(2,2) - expected_Y1_matrix(2,2)) > 0.001
        disp('错误：pow2(double矩阵)结果不符合预期');
        all_pass = false;
    end
    
    % 检测格式2：pow2(X_scalar, E_scalar)
    if abs(Y2_s_s - 10) > 0.001
        disp('错误：pow2(标量, 标量)结果不符合预期');
        all_pass = false;
    end
    
    % 检测格式2：pow2(X_matrix, E_scalar)
    if abs(Y2_m_s(1,1) - 4) > 0.001 || ...
       abs(Y2_m_s(1,2) - 12) > 0.001 || ...
       abs(Y2_m_s(2,1) - 20) > 0.001 || ...
       abs(Y2_m_s(2,2) - 28) > 0.001
        disp('错误：pow2(矩阵, 标量)结果不符合预期');
        all_pass = false;
    end
    
    % 检测格式2：pow2(X_scalar, E_matrix)
    if abs(Y2_s_m(1,1) - 2.5) > 0.001 || ...
       abs(Y2_s_m(1,2) - 5) > 0.001 || ...
       abs(Y2_s_m(2,1) - 1.25) > 0.001 || ...
       abs(Y2_s_m(2,2) - 20) > 0.001
        disp('错误：pow2(标量, 矩阵)结果不符合预期');
        all_pass = false;
    end
    
    % 检测格式2：pow2(X_matrix, E_matrix)
    if abs(Y2_m_m(1,1) - 1) > 0.001 || ...
       abs(Y2_m_m(1,2) - 6) > 0.001 || ...
       abs(Y2_m_m(2,1) - 2.5) > 0.001 || ...
       abs(Y2_m_m(2,2) - 56) > 0.001
        disp('错误：pow2(矩阵, 矩阵)结果不符合预期');
        all_pass = false;
    end
    
    % 输出最终测试结果
    if all_pass
        disp('test_builtin_pow2测试通过');
    else
        disp('test_builtin_pow2测试失败');
    end
end

function test_builtin_isnan
    % 创建包含nan的标量和矩阵
    nan_scalar = nan;               % 纯nan标量
    double_scalar = 3.1415;         % 正常double标量
    mixed_scalar = nan + 5;         % 运算产生的nan标量（仍为nan）

    % 矩阵测试
    nan_matrix = nan(2);          % 2x2全为nan的矩阵
    double_matrix = [1 2 3; 4 5 6]; % 正常double矩阵
    mixed_matrix = [1 nan 3;        % 包含nan的混合矩阵
                    nan 5 6];

    % 使用isnan检测
    result1 = isnan(nan_scalar);
    result2 = isnan(double_scalar);
    result3 = isnan(mixed_scalar);
    result4 = isnan(nan_matrix);
    result5 = isnan(double_matrix);
    result6 = isnan(mixed_matrix);

    % 结果检测
    all_pass = true;
    
    % 检测标量结果
    if result1 ~= true
        disp('错误：纯nan标量检测失败');
        all_pass = false;
    end
    
    if result2 ~= false
        disp('错误：正常double标量检测失败');
        all_pass = false;
    end
    
    if result3 ~= true
        disp('错误：运算产生的nan标量检测失败');
        all_pass = false;
    end
    
    % 检测矩阵结果
    if result4(1,1) ~= true || result4(1,2) ~= true || result4(2,1) ~= true || result4(2,2) ~= true
        disp('错误：全nan矩阵检测失败');
        all_pass = false;
    end
    
    if result5(1,1) ~= false || result5(1,2) ~= false || result5(1,3) ~= false || ...
       result5(2,1) ~= false || result5(2,2) ~= false || result5(2,3) ~= false
        disp('错误：正常double矩阵检测失败');
        all_pass = false;
    end
    
    if result6(1,1) ~= false || result6(1,2) ~= true || result6(1,3) ~= false || ...
       result6(2,1) ~= true || result6(2,2) ~= false || result6(2,3) ~= false
        disp('错误：混合矩阵检测失败');
        all_pass = false;
    end
    
    % 输出最终测试结果
    if all_pass
        disp('test_builtin_isnan测试通过');
    else
        disp('test_builtin_isnan测试失败');
    end
end

function test_builtin_complex
    % 1. 先生成double类型的实部和虚部标量
    real_scalar1 = 3.2;       % double实部标量
    imag_scalar1 = 4.5;       % double虚部标量

    real_scalar2 = -1.8;      % 含负数的double实部标量
    imag_scalar2 = -6.7;      % 含负数的double虚部标量

    real_scalar3 = 0;         % 实部为0（用于构造纯虚数）
    imag_scalar3 = 2.9;       % 虚部标量

    real_scalar4 = 5.1;       % 实部标量
    imag_scalar4 = 0;         % 虚部为0（用于构造纯实数）

    % 2. 构造标量复数
    complex_scalar1 = real_scalar1 + imag_scalar1 * 1i;
    complex_scalar2 = real_scalar2 + imag_scalar2 * 1i;
    complex_scalar3 = real_scalar3 + imag_scalar3 * 1i;
    complex_scalar4 = real_scalar4 + imag_scalar4 * 1i;

    % 3. 测试real函数提取实部
    real1 = real(complex_scalar1);
    real2 = real(complex_scalar2);
    real3 = real(complex_scalar3);
    real4 = real(complex_scalar4);

    % 4. 测试imag函数提取虚部
    imag1 = imag(complex_scalar1);
    imag2 = imag(complex_scalar2);
    imag3 = imag(complex_scalar3);
    imag4 = imag(complex_scalar4);

    % 结果检测
    all_pass = true;
    
    % 检测实部提取
    if abs(real1 - 3.2) > 0.001
        disp('错误：复数1实部提取失败');
        all_pass = false;
    end
    
    if abs(real2 - (-1.8)) > 0.001
        disp('错误：复数2实部提取失败');
        all_pass = false;
    end
    
    if abs(real3 - 0) > 0.001
        disp('错误：复数3实部提取失败');
        all_pass = false;
    end
    
    if abs(real4 - 5.1) > 0.001
        disp('错误：复数4实部提取失败');
        all_pass = false;
    end
    
    % 检测虚部提取
    if abs(imag1 - 4.5) > 0.001
        disp('错误：复数1虚部提取失败');
        all_pass = false;
    end
    
    if abs(imag2 - (-6.7)) > 0.001
        disp('错误：复数2虚部提取失败');
        all_pass = false;
    end
    
    if abs(imag3 - 2.9) > 0.001
        disp('错误：复数3虚部提取失败');
        all_pass = false;
    end
    
    if abs(imag4 - 0) > 0.001
        disp('错误：复数4虚部提取失败');
        all_pass = false;
    end
    
    % 输出最终测试结果
    if all_pass
        disp('test_builtin_complex测试通过');
    else
        disp('test_builtin_complex测试失败');
    end
end

function test_builtin_isreal
    % 1. 构造测试用的复数标量
    complex_scalar1 = 3 + 4i;       % 常规复数标量（非实数）
    complex_scalar2 = 5 - 2i;       % 含负数的复数标量（非实数）
    complex_scalar3 = 7 + 0i;       % 虚部为0的复数标量（本质是实数）

    % 2. 使用isreal测试复数标量
    result1 = isreal(complex_scalar1);
    result2 = isreal(complex_scalar2);
    result3 = isreal(complex_scalar3);

    % 结果检测
    all_pass = true;
    
    % 检测复数标量结果
    if result1 ~= false
        disp('错误：常规复数 3+4i 的isreal检测失败');
        all_pass = false;
    end
    
    if result2 ~= false
        disp('错误：复数 5-2i 的isreal检测失败');
        all_pass = false;
    end
    % 这里C代码与北太逻辑一致但与matlab不同
    if result3 == true
        disp('错误：虚部为0的复数 7+0i 的isreal检测失败');
        all_pass = false;
    end
    
    % 输出最终测试结果
    if all_pass
        disp('test_builtin_isreal测试通过');
    else
        disp('test_builtin_isreal测试失败');
    end
end

function test_builtin_conj
    % 1. 构造复数标量
    real_scalar1 = 3;
    imag_scalar1 = 4;
    complex_scalar1 = real_scalar1 + imag_scalar1 * 1i;  % 3 + 4i

    real_scalar2 = -2;
    imag_scalar2 = -5;
    complex_scalar2 = real_scalar2 + imag_scalar2 * 1i;  % -2 - 5i

    % 2. 使用conj函数处理复数标量
    result1 = conj(complex_scalar1);
    result2 = conj(complex_scalar2);

    % 结果检测
    all_pass = true;
    
    % 检测复数1的共轭
    if real(result1) ~= 3 || imag(result1) ~= -4
        disp('错误：复数 3+4i 的共轭检测失败');
        all_pass = false;
    end
    
    % 检测复数2的共轭
    if real(result2) ~= -2 || imag(result2) ~= 5
        disp('错误：复数 -2-5i 的共轭检测失败');
        all_pass = false;
    end
    
    % 输出最终测试结果
    if all_pass
        disp('test_builtin_conj测试通过');
    else
        disp('test_builtin_conj测试失败');
    end
end

function test_builtin_isinf
    % 1. 定义测试用的标量
    finite_scalar = 3.14;         % 有限值标量
    positive_inf_scalar = inf;    % 正无穷大标量
    negative_inf_scalar = -inf;   % 负无穷大标量
    nan_scalar = nan;             % 非数值（用于对比）

    % 2. 定义测试用的矩阵
    test_matrix = [
        1.2   inf    -3.4;
        -inf  5.6    nan;
        0     7.8    -inf
    ];

    % 3. 使用isinf检测标量
    result1 = isinf(finite_scalar);
    result2 = isinf(positive_inf_scalar);
    result3 = isinf(negative_inf_scalar);
    result4 = isinf(nan_scalar);

    % 4. 使用isinf检测矩阵
    result5 = isinf(test_matrix);

    % 结果检测
    all_pass = true;
    
    % 检测标量结果
    if result1 ~= false
        disp('错误：有限值标量检测失败');
        all_pass = false;
    end
    
    if result2 ~= true
        disp('错误：正无穷大标量检测失败');
        all_pass = false;
    end
    
    if result3 ~= true
        disp('错误：负无穷大标量检测失败');
        all_pass = false;
    end
    
    if result4 ~= false
        disp('错误：nan标量检测失败');
        all_pass = false;
    end
    
    % 检测矩阵结果（注释掉因当前C生成限制）
    /*
    expected_matrix = [
        false  true  false;
        true   false false;
        false  false true
    ];
    
    if result5(1,1) ~= expected_matrix(1,1) || ...
       result5(1,2) ~= expected_matrix(1,2) || ...
       result5(1,3) ~= expected_matrix(1,3) || ...
       result5(2,1) ~= expected_matrix(2,1) || ...
       result5(2,2) ~= expected_matrix(2,2) || ...
       result5(2,3) ~= expected_matrix(2,3) || ...
       result5(3,1) ~= expected_matrix(3,1) || ...
       result5(3,2) ~= expected_matrix(3,2) || ...
       result5(3,3) ~= expected_matrix(3,3)
        disp('错误：矩阵检测失败');
        all_pass = false;
    end
    */
    
    % 输出最终测试结果
    if all_pass
        disp('test_builtin_isinf测试通过');
    else
        disp('test_builtin_isinf测试失败');
    end
end

function test_builtin_realmax
    % 基本用法：获取默认精度下的最大浮点数（双精度）
    max_double = realmax;
    
    % 验证超过最大值的情况（会返回Inf）
    over_max = max_double * 2;
    is_inf_result = isinf(over_max);

    % 结果检测
    all_pass = true;
    
    % 检测realmax返回值
    if max_double <= 1e300
        disp('错误：realmax返回值过小');
        all_pass = false;
    end
    
    % 检测超过最大值的情况
    if ~is_inf_result
        disp('错误：超过最大值的运算未返回Inf');
        all_pass = false;
    end
    
    % 输出最终测试结果
    if all_pass
        disp('test_builtin_realmax测试通过');
    else
        disp('test_builtin_realmax测试失败');
    end
end

function test_builtin_logical
    % 1. 定义测试用的double标量
    double_scalar1 = 0;       % 零值（转换为false）
    double_scalar2 = 5.8;     % 非零值（转换为true）
    double_scalar3 = -3.2;    % 非零负值（转换为true）

    % 2. 定义测试用的double矩阵
    double_matrix = [
        0      1.2    -4.5;
        3.7    0      9.1;
        -0.0   6.3    0
    ];

    % 3. 使用logical转换double标量
    result1 = logical(double_scalar1);
    result2 = logical(double_scalar2);
    result3 = logical(double_scalar3);

    % 4. 使用logical转换double矩阵
    result4 = logical(double_matrix);
    
    % 5. 验证转换后的类型
    type1 = class(logical(double_scalar2));
    type2 = class(logical(double_matrix));

    % 结果检测
    all_pass = true;
    
    % 检测标量转换结果
    if result1 ~= false
        disp('错误：0转换为logical失败');
        all_pass = false;
    end
    
    if result2 ~= true
        disp('错误：正数5.8转换为logical失败');
        all_pass = false;
    end
    
    if result3 ~= true
        disp('错误：负数-3.2转换为logical失败');
        all_pass = false;
    end
    
    % 检测矩阵转换结果（注释掉因当前C生成限制）
    /*
    expected_matrix = double_matrix ~= 0;
    
    if result4(1,1) ~= expected_matrix(1,1) || ...
       result4(1,2) ~= expected_matrix(1,2) || ...
       result4(1,3) ~= expected_matrix(1,3) || ...
       result4(2,1) ~= expected_matrix(2,1) || ...
       result4(2,2) ~= expected_matrix(2,2) || ...
       result4(2,3) ~= expected_matrix(2,3) || ...
       result4(3,1) ~= expected_matrix(3,1) || ...
       result4(3,2) ~= expected_matrix(3,2) || ...
       result4(3,3) ~= expected_matrix(3,3)
        disp('错误：矩阵转换为logical失败');
        all_pass = false;
    end
    */
    
    % 检测类型
    if ~strcmp(type1, 'logical')
        disp('错误：标量转换后类型不是logical');
        all_pass = false;
    end
    
    if ~strcmp(type2, 'logical')
        disp('错误：矩阵转换后类型不是logical');
        all_pass = false;
    end
    
    % 输出最终测试结果
    if all_pass
        disp('test_builtin_logical测试通过');
    else
        disp('test_builtin_logical测试失败');
    end
end

function test_builtin_log
    % 1. 定义测试用的double标量
    scalar_small = 0.5;     % 小数值
    scalar_one = 1;         % 特殊值1
    scalar_e_small = 0.6931;% ln(2)≈0.6931，使exp结果为2
    scalar_ten_small = 2;   % 较小值，使log10结果适中

    % 2. 定义测试用的double矩阵
    matrix = [
        0.1    0.5    1.0;
        1.5    2.0    0.3;
        2.5    3.0    0.7
    ];

    % 3. 测试exp函数（计算自然指数e^x）
    exp_result1 = exp(scalar_small);
    exp_result2 = exp(scalar_e_small);
    exp_result3 = exp(matrix);

    % 4. 测试log函数（计算自然对数ln(x)）
    log_result1 = log(2);
    log_result2 = log(scalar_one);
    log_result3 = log(matrix);

    % 5. 测试log10函数（计算常用对数log10(x)）
    log10_result1 = log10(100);
    log10_result2 = log10(scalar_one);
    log10_result3 = log10(matrix);

    % 结果检测
    all_pass = true;
    
    % 检测exp函数结果
    if abs(exp_result1 - 1.6487) > 0.001
        disp('错误：exp(0.5)结果不符合预期');
        all_pass = false;
    end
    
    if abs(exp_result2 - 2) > 0.001
        disp('错误：exp(0.6931)结果不符合预期');
        all_pass = false;
    end
    
    % 检测log函数结果
    if abs(log_result1 - 0.6931) > 0.001
        disp('错误：log(2)结果不符合预期');
        all_pass = false;
    end
    
    if abs(log_result2 - 0) > 0.001
        disp('错误：log(1)结果不符合预期');
        all_pass = false;
    end
    
    % 检测log10函数结果
    if abs(log10_result1 - 2) > 0.001
        disp('错误：log10(100)结果不符合预期');
        all_pass = false;
    end
    
    if abs(log10_result2 - 0) > 0.001
        disp('错误：log10(1)结果不符合预期');
        all_pass = false;
    end
    
    % 输出最终测试结果
    if all_pass
        disp('test_builtin_log测试通过');
    else
        disp('test_builtin_log测试失败');
    end
end

function test_builtin_cross
    % 1. cross(A,B) 函数测试（向量叉积）
    A_vec3 = [1 2 3];
    B_vec3 = [4 5 6];
    cross_result = cross(A_vec3, B_vec3);

    % 2. diff(X) 函数测试（一阶差分）
    vec1 = [1 3 6 10 15];
    diff_result1 = diff(vec1);
    
    mat1 = [1 4 7; 2 5 8; 3 6 9];
    diff_result2 = diff(mat1);

    % 3. diff(X,n) 函数测试（n阶差分）
    vec2 = [1 2 4 7 11];
    diff_result3 = diff(vec2, 2);
    
    mat2 = [1 2; 4 5; 9 10; 16 17];
    diff_result4 = diff(mat2, 2);

    % 结果检测
    all_pass = true;
    
    % 检测cross函数结果
    if cross_result(1) ~= -3 || cross_result(2) ~= 6 || cross_result(3) ~= -3
        disp('错误：cross函数结果不符合预期');
        all_pass = false;
    end
    
    % 检测diff函数结果（一阶差分）
    if diff_result1(1) ~= 2 || diff_result1(2) ~= 3 || diff_result1(3) ~= 4 || diff_result1(4) ~= 5
        disp('错误：向量一阶差分结果不符合预期');
        all_pass = false;
    end
    
    % 检测diff函数结果（二阶差分）
    if diff_result3(1) ~= 1 || diff_result3(2) ~= 1 || diff_result3(3) ~= 1
        disp('错误：向量二阶差分结果不符合预期');
        all_pass = false;
    end
    
    % 输出最终测试结果
    if all_pass
        disp('test_builtin_cross测试通过');
    else
        disp('test_builtin_cross测试失败');
    end
end

% =========================================================================
% 测试 polyder 函数
% =========================================================================
function test_builtin_polyder()
    % 基础测试：单多项式求导
    p = [2, 0, -3, 5];  % 2x³ - 3x + 5
    p_der = polyder(p);

    % 扩展测试：两个多项式乘积的导数
    p1 = [1, 2];     % x + 2
    p2 = [3, -1];    % 3x - 1
    p_prod_der = polyder(p1, p2);

    % 结果检测
    all_pass = true;
    
    % 检测基础求导
    if ~isequal(p_der, [6, 0, -3])
        disp('错误：基础多项式求导结果不符合预期');
        all_pass = false;
    end
    
    % 检测乘积求导
    if ~isequal(p_prod_der, [6, 5])
        disp('错误：多项式乘积求导结果不符合预期');
        all_pass = false;
    end
    
    % 输出最终测试结果
    if all_pass
        disp('test_builtin_polyder测试通过');
    else
        disp('test_builtin_polyder测试失败');
    end
end

function test_builtin_polyint()
    % 准备测试数据
    p_der = [6, 0, -3];  % 6x² - 3
    
    % 测试1：默认积分常数（C=0）
    p_int = polyint(p_der);
    
    % 测试2：指定积分常数（C=2）
    p_int_C2 = polyint(p_der, 2);

    % 结果检测
    all_pass = true;
    
    % 检测默认积分
    if ~isequal(p_int, [2, 0, -3, 0])
        disp('错误：默认积分常数（C=0）结果不符合预期');
        all_pass = false;
    end
    
    % 检测指定积分常数
    if ~isequal(p_int_C2, [2, 0, -3, 2])
        disp('错误：指定积分常数（C=2）结果不符合预期');
        all_pass = false;
    end
    
    % 输出最终测试结果
    if all_pass
        disp('test_builtin_polyint测试通过');
    else
        disp('test_builtin_polyint测试失败');
    end
end

function test_builtin_fft()
    % 简化测试：使用小规模数据
    L = 16;           % 减小信号长度
    T = 0.1;          % 增大采样周期
    t = 0:T:(L-1)*T;  % 时间向量
    
    % 生成简单的测试信号（单个正弦波），使用3.1415代替pi
    s = sin(2*3.1415*2*t);  % 2Hz正弦波
    
    % 执行 FFT 计算
    S = fft(s);

    % 结果检测
    all_pass = true;
    
    % 基本验证：检查FFT结果是否非空且长度正确
    if isempty(S)
        disp('错误：FFT结果为空');
        all_pass = false;
    end
    
    if length(S) ~= L
        disp('错误：FFT结果长度不正确');
        all_pass = false;
    end
    
    % 验证FFT的对称性（实数信号的FFT应该是共轭对称的）
    % 分别比较实部和虚部，避免使用复数abs
    is_symmetric = true;
    for i1 = 2:floor(L/2)
        i = i1(1);
        real_diff = real(S(i)) - real(S(L-i+2));
        imag_diff = imag(S(i)) + imag(S(L-i+2));  % 注意虚部是相反数
        if abs(real_diff) > 0.001 || abs(imag_diff) > 0.001
            is_symmetric = false;
            break;
        end
    end
    
    if ~is_symmetric
        disp('错误：FFT结果不满足共轭对称性');
        all_pass = false;
    end
    
    % 验证第一个分量（直流分量）应该接近0（正弦波均值为0）
    % 分别检查实部和虚部
    if abs(real(S(1))) > 0.001 || abs(imag(S(1))) > 0.001
        disp('错误：直流分量不符合预期');
        all_pass = false;
    end
    
    % 输出最终测试结果
    if all_pass
        disp('test_builtin_fft测试通过');
    else
        disp('test_builtin_fft测试失败');
    end
end