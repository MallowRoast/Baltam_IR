function test4_2
    [a,b,c,d] = varargout_function(1,2,3);
    if ~(a==1&&b==1&&c==2&&d==3)
        error("21_1");
    end
    
    [a,b,c] = varargout_function2(1,2,3);
    if ~(a==1&&b==2&&c==3)
        error("21_2");
    end
    
    [a,c] = varargout_function3(1,2,3);
    if ~(a==-1&&c==1)
        error("21_3");
    end
end

% 正常的可变参数写法
function [c,varargout] = varargout_function(varargin)
    for i = 1:min(nargout-1, nargin)
        varargout{i} = varargin{i};
    end
    c = varargout{1};
end

% 正常的可变参数写法
function varargout = varargout_function2(varargin)
    for i = 1:min(nargout, nargin)
        varargout{i} = varargin{i};
    end
end

% 错误使用的可变参数写法 注意此刻varargout不是可变输出
function [varargout1, c] = varargout_function3(varargin) % 错误，varargout应在最后
    c=1
    varargout1 = -1 
end
