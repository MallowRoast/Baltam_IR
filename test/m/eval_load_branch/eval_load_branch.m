function result = eval_load_branch(use_eval, file)
    % 两条分支都可能修改 x，汇合后读取修改后的值。
    x = 1;
    if use_eval
        code = 'x = 2;';
        eval(code);
    else
        % MAT 文件可以包含变量 x；未包含时保留原值。
        load(file);
    end
    result = x;
end
