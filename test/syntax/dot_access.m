function dot_access()
    a.b;  % 左侧名字尚未绑定。
    a = struct();  % 按名字调用 struct，结果写入 a。
    x = a.b;  % 已绑定变量的静态字段读取。
    a.b = 2;  % 更新字段后写回 a。
    y = a.('b');  % 动态字段名表达式。
end
