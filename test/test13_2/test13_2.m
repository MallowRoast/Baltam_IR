% 更多结构体函数

function test13_2

myStruct.a = 1;
myStruct.bb = 2.2;
myStruct.c = 3;

% fieldnames 获取结构体的字段名称
fields = fieldnames(myStruct);
if ~strcmp(fields{1}, 'a') | ~strcmp(fields{2}, 'bb') | ~strcmp(fields{3}, 'c')
    error('1');
end

% *.(*)
fielName = ['b' char(99-1)];
fieldValue = myStruct.(fielName); % node_struct_get(node_name, node_name)
if fieldValue ~= 2.2
    error('2');
end

% getfield 结构体数组字段
if ~(getfield(myStruct, 'a') == 1 || getfield(myStruct, 'bb') == 2.2 || getfield(myStruct, 'c') == 3)
    error('3');
end

% isfield 确定输入是否为结构体数组字段
if ~(isfield(myStruct, 'a')||isfield(myStruct, 'bb')||isfield(myStruct, 'c'))
    error('4')
end 

% isstruct 确定输入是否为结构体数组
if ~isstruct(myStruct)
    error('5')
end

% orderfields 结构体数组的顺序字段
myStruct = struct('a', 1, 'bb', 2.2, 'c', 3);
expectedOrder = {'a', 'bb', 'c'};
reorderedStruct = orderfields(myStruct, expectedOrder);
actualOrder = fieldnames(reorderedStruct);
actualOrder = actualOrder';
if ~isequal(actualOrder, expectedOrder)
    error('6');
end

% rmfield 删除结构体中的字段
newStruct = rmfield(myStruct, 'bb');
if isfield(newStruct, 'bb')
    error('7');
end

% setfield 为结构体数组字段赋值
updatedStruct = setfield(myStruct, 'a', 10);
if updatedStruct.a ~= 10
    error('8');
end

% structfun 对标量结构体的每个字段应用函数
% myStruct2.a = 1;
% myStruct2.bb = 2.2;
% myStruct2.c = 3;
% resultStruct = structfun(@doubleNumeric, myStruct2);

% if ~(resultStruct(1) == 2 || resultStruct(2)  == 4.4 || resultStruct(3)  == 6)
%     error('9');
% end

% cell2struct 将元胞数组转换为结构体数组
cellData = {10, 20.2, 30};
fieldNames = {'a', 'bb', 'c'};
newStructFromCell = cell2struct(cellData, fieldNames, 2);
for i = 1:length(fieldNames)
    fieldName = fieldNames{i};
    if newStructFromCell.(fieldName) ~= cellData{i}
        error('10');
    end
end

% struct2cell 将结构体转换为元胞数组
cellFromStruct = struct2cell(myStruct);
for i = 1:length(fieldnames(myStruct))
    fieldName = fieldnames(myStruct){i};
    if ~isequal(cellFromStruct{i}, myStruct.(fieldName))
        error('11');
    end
end

end

function output = doubleNumeric(input)
    if isnumeric(input)
        output = 2 * input;
    else
        output = input;
    end
end
