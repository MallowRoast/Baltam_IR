% for 和 while 循环的嵌套
function test9_3
    % for
    s = 0;
    for i = 1:5
        s = s + i;
        if rem(i, 2) == 0
            continue;
        elseif rem(i, 4) == 0
            break;
        end
    end
    if s ~= 15
        error('1');
    end

    for i = 1:8
        s = s + i;
        for j = 1:5
            s = s + j;
        end
        my_rng = linspace(2,5,4);
        for k = my_rng
            k = k-2;
            i = i - k;
            s = s + i + 2*k;
            my_rng = [];
        end
    end
    if s ~= 331
        error('2');
    end
    
    % while
    j = 1;
    while j < 10
        if rem(i, 2) == 0
            j = j + 1;
            continue;
        elseif rem(i, 4) == 0
            for k = my_rng
                k = k-2;
                i = i - k;
                while j < 5
                    s = s + j;
                    j = j + 2;
                end
                s = s + i + 2*k;
                my_rng = [];
            end
            break;
        end
        s = s + j;
        j = j + 1;
        while j < 5
            s = s + j;
            j = j + 2;
        end
        while j < 5
            s = s + j;
            j = j + 2;
        end
    end
    if s ~= 331
        error('3');
    end

    j = -2;
    while j < 5
        s = s + j;
        j = j + 2;
    end
    if s ~= 335
        error('4');
    end
end
