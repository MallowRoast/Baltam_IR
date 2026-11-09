function result = record_binding(a, condition)
    x = 0;
    y = a;
    % 同一个 SSA 值可绑定多个变量，条件赋值必须留在实际分支内。
    if condition
        x = y;
    end
    result = x;
end
