% 更多元胞数组语法
function test11_2
a = {1,2,'3',4};
b = a{:};
if b ~= 1
    error('10');
end

[b2, b3] = a{2:end}; % node_horz_list, node_asgn, node_cell_get
if b2 ~= 2 || b3 ~= '3'
    error('11');
end

c = cell(1,3);
% node_horz_list[node_cell_get,...] node_asgn node_cell_get
[c{1}, c{2}, c{3}] = a{:};
if c{1} ~= 1 || c{2} ~= 2 || c{3} ~= '3'
    error('12');
end

% node_horz_list[..., ..., node_struct_get(node_struct_get(f,name),age)] node_asgn ...
[d(1),a{1},f.name.age] = a{:};
if d(1) ~= 1 || a{1} ~= 2 || f.name.age ~= '3'
    error('13');
end

% node_brace_fenhao_list[node_list, node_list]
e = {1,a;'3',4};
if e{3}{2} ~= a{2} % cell_get(cell_get(e,3), 2)
    error('14');
end
e{3}{4} = 5; % cell_set(cell_get(e,3), 4, 5)
if e{3}{4} ~= 5
    error('15');
end

myargs = {1:2,2:3};
rng = 1:2;
% 只要函数调用入参出现 cell_get，就生成 _in.clear() << ...
[a,b] = deal(myargs{rng});
if ~isequal(a,1:2) || ~isequal(b,2:3)
    error('16');
end
end
