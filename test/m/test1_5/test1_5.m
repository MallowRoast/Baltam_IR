% switch-case 支持
function test1_5    
aa = 1;
c = 0;
fh = @(x)x+1;
switch (aa+fh(2))
    case 3
        c = 8;
    case {4,'a'}
        c = 5;
    case {5,6}
        c = 6;
    otherwise
end

if c ~= 5
    error('1');
end

if complexSwitchExample(0) ~= '零'
    error('2');
end
if ~strcmp(complexSwitchExample(3), '数值: 3')
    error('3');
end
if ~strcmp(complexSwitchExample('a'), '字母 A')
    error('4');
end
if ~strcmp(complexSwitchExample('d'), '字符: d')
    error('5');
end
if ~strcmp(complexSwitchExample({1, 'text'}), '元胞')
    error('6');
end

end

function result = complexSwitchExample(input)
switch class(input)
    case 'double'  
        result = processDouble(input);
    case 'char'    
        result = processChar(input);
    case 'logical' 
        result = processLogical(input);
    case 'cell'    
        result = processCell(input);
    otherwise       
        error('未处理的输入类型: %s', class(input));
end
end

function result = processDouble(input)
switch input
    case 0
        result = '零';
    case 1
        result = '一';
    case 2
        result = '二';
    otherwise
        result = ['数值: ', num2str(input)];
end
end

function result = processChar(input)
switch input
    case 'a'
        result = '字母 A';
    case 'b'
        result = '字母 B';
    case 'c'
        result = '字母 C';
    otherwise
        result = ['字符: ', input];
end
end

function result = processLogical(input)
    if input
        result = '逻辑真';
    else
        result = '逻辑假';
    end
end

function result = processCell(input)
    result = '元胞';
end
