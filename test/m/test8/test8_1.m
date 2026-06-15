% TEST8_1 覆盖脚本中无法静态确定 magic end 归属的 unresolved apply。

single_value = A(end);
nested_value = A(fun(end));
