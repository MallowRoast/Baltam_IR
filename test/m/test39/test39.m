% C 生成性能测试（傅里叶变换）
% 迭代实现的基 2-FFT 算法（比特反转重排序）
% 输入 x： 长度为 2^n 的向量
% 输出 X： x 的 DFT 结果

function test39()
    % 测试代码
    N = 2^9;  % 必须是2的幂次
    x = rand(1, N) + 1i * rand(1, N);  % 随机复数信号

    % 使用我们的实现
    X_my = my_fft_iterative(x, 1);

    x1 = my_fft_iterative(X_my, -1);
    
    error = max(abs(x - x1));
    fprintf('最大误差: %e\n', error);
    
    if (error > 1e-12)
        error('1');
    end

    % 使用MATLAB内置函数进行比较
    % X_matlab = fft(x);
    % error = max(abs(X_my - X_matlab));
    % fprintf('最大误差: %e\n', error);
end

function X = my_fft_iterative(x, direction)
% 迭代实现的基2-FFT算法（支持正向和反向FFT）
% 输入：x - 长度为2^n的向量
%        direction - 1 或 -1（可选，默认为1）
% 输出：X - x的DFT或IDFT结果

% 设置默认方向为正向FFT
if nargin < 2
    direction = 1;
end

N = length(x);

% 检查输入长度是否为2的幂次
if bitand(N, N-1) ~= 0
    error('输入长度必须是2的幂次');
end

% 进行比特反转重排序
X = bit_reverse_order(x);

% 根据方向选择符号
if direction == 1
    sign_factor = -1;  % 正向FFT：e^{-j2π/N}
    scale_factor = 1;  % 正向FFT不缩放
elseif direction == -1
    sign_factor = 1;   % 反向FFT：e^{+j2π/N}
    scale_factor = 1/N; % 反向FFT需要除以N
else
    error('direction参数必须是 1 或 -1');
end

% 迭代计算FFT
for s = 1:log2(N)  % 阶段
    m = 2^s;  % 当前蝶形运算的跨度
    W_m = exp(sign_factor * 1i * 2*pi/m);  % 当前阶段的旋转因子基数
    
    for k = 0:m:N-1  % 每组蝶形运算
        W = 1;  % 初始化旋转因子
        for j = 0:m/2-1  % 每个蝶形运算
            t = W * X(k + j + m/2 + 1);  % 注意MATLAB索引从1开始
            u = X(k + j + 1);
            
            % 蝶形运算
            X(k + j + 1) = u + t;
            X(k + j + m/2 + 1) = u - t;
            
            % 更新旋转因子
            W = W * W_m;
        end
    end
end

% 应用缩放因子（仅反向FFT需要）
if scale_factor ~= 1
    X = X * scale_factor;
end

end

function y = bit_reverse_order(x)
% 比特反转重排序
N = length(x);
n_bits = log2(N);

% 创建索引数组
indices = 0:N-1;

% 计算每个索引的比特反转值
rev_indices = zeros(1, N);
for i = 0:N-1
    % 手动进行比特反转
    rev = 0;
    for b = 0:n_bits-1
        if bitand(i, bitshift(1, b))  % 检查第b位是否为1
            rev = bitor(rev, bitshift(1, n_bits-1-b));  % 设置对应位
        end
    end
    rev_indices(i+1) = rev;
end

% 重新排序
y = x(rev_indices + 1);  % 注意MATLAB索引从1开始
end
