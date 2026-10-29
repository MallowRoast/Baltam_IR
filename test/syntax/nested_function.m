function y = nested_function(x)
    y = inner(x);

    function z = inner(v)
        z = v + 1;

        function w = leaf(t)
            w = t * 2;
        end
    end
end

function y = local_helper(x)
    y = x - 1;
end
