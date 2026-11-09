function result = for_with_eval(values)
    x = 0;
    for i = values
        x = i;
        code = 'x = 99;';
        eval(code);
    end
    result = x;
end
