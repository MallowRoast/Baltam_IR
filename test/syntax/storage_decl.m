function storage_decl()
    global x y  % 声明两个全局变量。
    persistent p q  % 声明两个持久变量。
    x = 1;  % 写入全局变量。
    p = 2;  % 写入持久变量。
    y = x;  % 读取全局变量并写入另一个全局变量。
    q = p;  % 读取持久变量并写入另一个持久变量。
end
