function test23_failure()
    % 测试快速排序算法的函数
    fprintf('===== 快速排序测试开始 =====\n\n');
    
    % 测试用例：不同规模和特性的数组
    test_cases = {
        % 空数组
        {[], '空数组'},
        % 单元素数组
        {[5], '单元素数组'},
        % 已排序数组
        {[1, 2, 3, 4, 5], '已排序数组'},
        % 逆序数组
        {[5, 4, 3, 2, 1], '逆序数组'},
        % 重复元素数组
        {[3, 3, 3, 3, 3], '全重复元素数组'},
        % 部分重复元素数组
        {[2, 2, 4, 4, 1, 1], '部分重复元素数组'},
        % 随机小数数组
        {rand(1, 10), '随机小数数组'},
        % 大随机整数数组
        {randi([-1000, 1000], 1, 1000), '大随机整数数组'}
    };
    
    % 对每个测试用例进行测试
    for i = 1:length(test_cases)
        test_case = test_cases{i};
        arr = test_case{1};
        case_name = test_case{2};
        
        fprintf('测试 %d: %s\n', i, case_name);
        
        % 复制数组以避免修改原始数据
        test_arr = arr;
        
        % 计算排序前的统计信息
        if ~isempty(test_arr)
            fprintf('  排序前: 最小值 = %.2f, 最大值 = %.2f, 长度 = %d\n', ...
                min(test_arr), max(test_arr), length(test_arr));
        else
            fprintf('  排序前: 空数组\n');
        end
        
        % 测试快速排序
        tic;
        sorted_quick = quick_sort(test_arr);
        quick_time = toc;
        
        % 使用MATLAB内置排序函数作为参考
        tic;
        sorted_matlab = sort(test_arr);
        matlab_time = toc;
        
        % 验证排序结果
        is_correct = isequal(sorted_quick, sorted_matlab);
        
        % 输出排序后的统计信息
        if ~isempty(sorted_quick)
            fprintf('  排序后: 最小值 = %.2f, 最大值 = %.2f, 长度 = %d\n', ...
                min(sorted_quick), max(sorted_quick), length(sorted_quick));
        else
            fprintf('  排序后: 空数组\n');
        end
        
        % 输出排序时间和结果正确性
        fprintf('  快速排序时间: %.6f 秒\n', quick_time);
        fprintf('  MATLAB内置排序时间: %.6f 秒\n', matlab_time);
        
        % 计算速度比
        if matlab_time > 0
            speed_ratio = quick_time / matlab_time;
            fprintf('  速度比 (快速排序/MATLAB排序): %.2f\n', speed_ratio);
        end
        
        fprintf('\n');
    end
    
    % 性能测试：不同规模的随机数组
    fprintf('===== 性能测试 =====\n');
    array_sizes = [10, 100, 1000, 10000, 50000];
    num_trials = 5;  % 每个规模测试多次取平均值
    
    fprintf('数组规模\t快速排序时间(秒)\tMATLAB排序时间(秒)\t速度比\n');
    
    for i = 1:length(array_sizes)
        size = array_sizes(i);
        quick_times = zeros(num_trials, 1);
        matlab_times = zeros(num_trials, 1);
        
        for trial = 1:num_trials
            % 生成随机数组
            test_arr = randi([1, 10000], 1, size);
            
            % 测试快速排序
            tic;
            quick_sort(test_arr);
            quick_times(trial) = toc;
            
            % 测试MATLAB排序
            tic;
            sort(test_arr);
            matlab_times(trial) = toc;
        end
        
        % 计算平均时间
        avg_quick_time = mean(quick_times);
        avg_matlab_time = mean(matlab_times);
        avg_speed_ratio = avg_quick_time / avg_matlab_time;
        
        fprintf('%d\t\t%.6f\t\t%.6f\t\t%.2f\n', ...
            size, avg_quick_time, avg_matlab_time, avg_speed_ratio);
    end
    
    fprintf('\n===== 快速排序测试完成 =====\n');
end



function sorted_array = quick_sort(arr)
    % 快速排序主函数
    % 输入: arr - 待排序的数组
    % 输出: sorted_array - 排序后的数组
    
    if nargin == 0
        error('至少需要一个输入参数');
    end
    
    % 复制输入数组，避免修改原始数据
    sorted_array = arr;
    
    % 调用递归排序函数
    if length(sorted_array) > 1
        sorted_array = quick_sort_recursive(sorted_array, 1, length(sorted_array));
    end
end

function arr = quick_sort_recursive(arr, low, high)
    % 递归实现快速排序
    % 输入: arr - 待排序的数组
    %       low - 当前子数组的起始索引
    %       high - 当前子数组的结束索引
    % 输出: arr - 排序后的子数组
    
    if low < high
        % 分区操作，获取分区点
        pivot_index = partition(arr, low, high);
        
        % 递归排序左右两部分
        arr = quick_sort_recursive(arr, low, pivot_index - 1);
        arr = quick_sort_recursive(arr, pivot_index + 1, high);
    end
end

function pivot_index = partition(arr, low, high)
    % 分区函数，选择最后一个元素作为基准
    % 输入: arr - 待分区的数组
    %       low - 当前子数组的起始索引
    %       high - 当前子数组的结束索引
    % 输出: pivot_index - 基准元素的最终位置
    
    % 选择最后一个元素作为基准
    pivot = arr(high);
    i = low - 1;
    
    % 将小于基准的元素放到左边，大于基准的元素放到右边
    for j = low:high-1
        if ~(arr(j) > pivot)
            i = i + 1;
            % 交换元素
            temp = arr(i);
            arr(i) = arr(j);
            arr(j) = temp;
        end
    end
    
    % 将基准元素放到正确的位置
    temp = arr(i + 1);
    arr(i + 1) = arr(high);
    arr(high) = temp;
    
    % 返回基准元素的位置
    pivot_index = i + 1;
end