function test23()
    disp('===== 快速排序性能测试：不同规模数组的排序时间 =====');
    disp('');
    
    % 定义测试的数组规模（可根据需要调整）
    array_sizes = [10, 100, 1000, 10000];
    
    % 表头
    disp('数组规模    最小排序时间(秒)');
    
    % 对每个规模进行测试
    for i = 1:length(array_sizes)
        r = rng('default'); % 重置随机种子

        size = array_sizes(i);
        
        % 生成随机整数数组
        test_arr = randi([-1e6, 1e6], 1, size);
        % 重复测试多次以获得稳定结果
        num_trials = 3;
        times = zeros(num_trials, 1);
        % 执行测试
        for trial = 1:num_trials
            % 复制数组以确保每次测试数据相同
            arr_copy = test_arr;
            tic;
            quick_sort(arr_copy);
            times(trial) = toc;
        end
        
        % 计算最小时间（排除可能的异常值）
        avg_time = min(times);
        
        % 输出结果
        disp([num2str(size) '    ' num2str(avg_time, '%.6f')]);
    end
    
    disp('');
    disp('===== 测试完成 =====');
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
