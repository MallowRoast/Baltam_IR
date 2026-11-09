function result = internal_slot(values, a, b)
    result = 0;
    for i = values
        % 短路结果直接通过 PHI 汇合，循环计数器仍使用内部槽位。
        for j = (a && b)
            result = i;
        end
    end
end
