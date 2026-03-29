% 西电演示波形极值

function test36()
    % 生成带噪声的测试信号
    fs = 1000; % 采样频率
    t = 0 : 1/fs : 0.5; % 时间向量
    f1 = 5; % 基频
    f2 = 15; % 高频分量

    % 原始信号
    clean_signal = sin(2*pi*f1*t) + 0.5*cos(2*pi*f2*t);

    % 添加噪声
    noise_level = 0.02;
    noisy_signal = clean_signal + noise_level * randn(size(t));

    % 计算极值
    smooth_factor = 0.05;
    min_prominence = 0.02 * (max(noisy_signal) - min(noisy_signal));
    min_distance = 5;
    [max_locs, min_locs] = find_extrema_robust(noisy_signal, smooth_factor, min_prominence, min_distance);

    disp('min_locs: ');
    disp(min_locs);
    disp('max_locs: ');
    disp(max_locs);

    % 画图
    % figure; plot(noisy_signal, '.-'); hold on;
    % scatter(max_locs, noisy_signal(max_locs), 'ro');
    % scatter(min_locs, noisy_signal(min_locs), 'r^');
end

% 鲁棒的极值检测算法，可处理噪声
% 输入: signal - 等间距信号
% 可选参数:
%   'smooth_factor' - 平滑参数 (默认: 0.1)
%   'min_prominence' - 最小显著度 (默认: 0.1 * 信号范围)
%   'min_distance' - 极值间最小距离 (默认: 5个采样点)
function [max_locs, min_locs, max_vals, min_vals] = find_extrema_robust(signal, smooth_factor, min_prominence, min_distance)
    
    % 步骤1: 平滑信号去除噪声
    if smooth_factor > 0
        smoothed_signal = smooth_signal(signal, smooth_factor);
    else
        smoothed_signal = signal;
    end

    % 步骤2: 找极值
    [max_vals, max_locs] = my_findpeaks(smoothed_signal, min_prominence, min_distance);

    [min_vals, min_locs] = my_findpeaks(-smoothed_signal, min_prominence, min_distance);
    min_vals = -min_vals;
end

% 使用滑动平均滤波器平滑信号
function smoothed = smooth_signal(signal, factor)
    window_size = max(3, round(numel(signal) * factor));
    if mod(window_size, 2) == 0
        window_size = window_size + 1;
    end
    smoothed = my_movmean(signal, window_size);
end


function y = my_movmean(x, k)
% 实现内建函数 movemean()
%MY_MOVMEAN  Compute moving average of vector x with window size k
%
%   y = MY_MOVMEAN(x, k)
%
%   Inputs:
%       x - numeric vector
%       k - window size (positive integer)
%
%   Output:
%       y - vector of same size as x, containing moving average

    % 输入检查
    % if nargin < 2
    %     error('Usage: y = my_movmean(x, k)');
    % end
    % if ~isvector(x)
    %     error('Input x must be a vector.');
    % end
    % if ~isscalar(k) || k <= 0 || floor(k) ~= k
    %     error('Window size k must be a positive integer.');
    % end

    n = length(x);
    y = zeros(size(x));
    half = floor(k/2);

    % 对边界进行处理（类似 movmean 的 behavior）
    for i = 1:n
        % 计算窗口范围
        left = max(1, i - half);
        right = min(n, i + half);
        y(i) = mean(x(left:right));
    end
end


function [peaks, locations] = my_findpeaks(signal, min_prominence, min_distance)
    % 简单的极值检测函数（仿照内置函数 findpeaks）
    % 输入:
    %   signal - 输入信号
    %  可选参数:
    %   'MinPeakHeight' - 最小峰值高度 (默认: -inf)
    min_height = -inf;
    %   'MinPeakProminence' - 最小峰值显著度 (默认: 0)
    %   'MinPeakDistance' - 峰值间最小距离 (默认: 1)
    %   'Threshold' - 阈值，用于判断峰值显著性 (默认: 0)
    threshold = 0;
    %   'NPeaks' - 要找到的峰值数量 (默认: 全部)
    n_peaks = inf;

    % 找到所有候选峰值位置
    candidate_locs = find_peak_candidates(signal, threshold);

    % 提取候选峰值
    candidate_peaks = signal(candidate_locs);

    % 应用高度过滤
    height_mask = candidate_peaks >= min_height;
    candidate_locs = candidate_locs(height_mask);
    candidate_peaks = candidate_peaks(height_mask);

    % 应用显著度过滤
    if min_prominence > 0
        prominence_mask = calculate_prominence(signal, candidate_locs) >= min_prominence;
        candidate_locs = candidate_locs(prominence_mask);
        candidate_peaks = candidate_peaks(prominence_mask);
    end

    % 应用距离过滤
    if min_distance > 1 && numel(candidate_locs) > 1
        [candidate_locs, candidate_peaks] = apply_min_distance(...
            candidate_locs, candidate_peaks, min_distance);
    end

    % 限制峰值数量
    if ~isinf(n_peaks) && numel(candidate_peaks) > n_peaks
        [~, sort_idx] = sort(candidate_peaks, 'descend');
        candidate_locs = candidate_locs(sort_idx(1:n_peaks));
        candidate_peaks = candidate_peaks(sort_idx(1:n_peaks));
        [candidate_locs, sort_idx2] = sort(candidate_locs);
        candidate_peaks = candidate_peaks(sort_idx2);
    end

    % 输出结果
    peaks = candidate_peaks;
    locations = candidate_locs;
end

function peak_locs = find_peak_candidates(signal, threshold)
    % 找到峰值候选位置
    n = numel(signal);
    peak_locs = [];

    for i = 2:n-1
        % 检查是否为局部极大值
        if signal(i) > signal(i-1) && signal(i) > signal(i+1)
            % 检查是否超过阈值
            left_diff = signal(i) - signal(i-1);
            right_diff = signal(i) - signal(i+1);
            if left_diff > threshold && right_diff > threshold
                peak_locs = [peak_locs, i];
            end
        end
    end
end

function prominence = calculate_prominence(signal, peak_locs)
    % 计算峰值显著度
    n_peaks = numel(peak_locs);
    prominence = zeros(1, n_peaks);

    for i = 1:n_peaks
        loc = peak_locs(i);
        peak_val = signal(loc);

        % 向左找最低点
        left_min = inf;
        for j = loc-1:-1:1
            if signal(j) < left_min
                left_min = signal(j);
            end
            if signal(j) > peak_val
                break;
            end
        end

        % 向右找最低点
        right_min = inf;
        for j = loc+1:numel(signal)
            if signal(j) < right_min
                right_min = signal(j);
            end
            if signal(j) > peak_val
                break;
            end
        end

        % 计算显著度
        prominence(i) = peak_val - max(left_min, right_min);
    end
end

function [new_locs, new_peaks] = apply_min_distance(locs, peaks, min_distance)
    % 应用最小距离约束
    [~, sort_idx] = sort(peaks, 'descend');
    sorted_locs = locs(sort_idx);
    sorted_peaks = peaks(sort_idx);

    new_locs = [];
    new_peaks = [];

    for i = 1:numel(sorted_locs)
        current_loc = sorted_locs(i);
        current_peak = sorted_peaks(i);

        % 检查是否与已选峰值距离足够
        too_close = false;
        for j = 1:numel(new_locs)
            if abs(current_loc - new_locs(j)) < min_distance
                too_close = true;
                break;
            end
        end

        if ~too_close
            new_locs = [new_locs, current_loc];
            new_peaks = [new_peaks, current_peak];
        end
    end

    % 按位置排序输出
    [new_locs, sort_idx] = sort(new_locs);
    new_peaks = new_peaks(sort_idx);
end
