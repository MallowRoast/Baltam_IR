% 内建函数测试（马崇耘）diag isdiag eye length tril triu inv fix mod ./ & qr lu det rank 的测试（TODO: 需要自动化检查和报错）
function test33_3
    test_builtin_length();
    test_builtin_diag();
    test_builtin_eye();
    test_builtin_tril();
    test_builtin_triu();
    test_builtin_inv();
    test_builtin_fix();
    test_builtin_mod();
    test_builtin_rdivide();
    test_builtin_and();
    test_builtin_qr();
    test_builtin_lu();
    test_builtin_det();
    test_builtin_rank();
    
    disp("所有测试通过！");
end

function test_builtin_length
    a = [1 -2 4; -5 2 0; 1 0 3];
    b = length(a);
    if b ~= 3
        error('length 错误');
    end
end

function test_builtin_diag
    v = [1, 2, 3];
    c = diag(v);
    if ~isdiag(c)
        error('isdiag 错误');
    end
    if ~isequal(diag(c), v.')
        error('isdiag 错误');
    end
    c2 = diag(v,2);
    if ~isequal(diag(c2,2), v.')
        error('isdiag 错误');
    end
end

function test_builtin_eye
    e1 = eye(3);
    d1 = diag(e1);
    diff1 = e1 - diag(d1);
    if ~isequal(d1, ones(3,1)) || ~isequal(diff1, zeros(3))
        error('eye 错误');
    end
    e2 = eye(3,4);
    if size(e2,1)~=3 || size(e2,2)~=4 || ~isequal(diag(e2), ones(3,1))
        error('eye 错误');
    end
end

function test_builtin_tril
    a = [1 2 3; 4 5 6; 7 8 9];
    if ~isequal(tril(a) + triu(a,1), a)
        error('tril 错误');
    end
end

function test_builtin_triu
    a = [1 2 3; 4 5 6; 7 8 9];
    if ~isequal(tril(a) + triu(a,1), a)
        error('triu 错误');
    end
end

function test_builtin_inv
    a = [1,2,3; 1,3,4; 2,2,3];
    e = inv(a);
    e2 = inv(e);
    
    % 不使用 max 检查误差
    diff_mat = e2 - a;
    error_found = 0;
    for i = 1:numel(diff_mat)
        if abs(diff_mat(i)) > 1e-13
            error_found = 1;
            break;
        end
    end
    if error_found
        error('inv 错误');
    end
end

function test_builtin_fix
    a = [1.33,2,3;2,3.57,4.9;5,2.77,3.19];
    f = fix(a);
    floor_a = floor(a);
    
    % 不使用 max 检查 fix 误差
    diff_fix = f - floor_a;
    error_found = 0;
    for i = 1:numel(diff_fix)
        if abs(diff_fix(i)) > 1
            error_found = 1;
            break;
        end
    end
    if error_found
        error('fix 错误');
    end
end

function test_builtin_mod
    a = [1.33,2,3;2,3.57,4.9;5,2.77,3.19];
    g = mod(a,3);
    
    % 不使用 min/max 检查 mod 范围
    error_found = 0;
    for i = 1:numel(g)
        if g(i) < 0 || g(i) >= 3
            error_found = 1;
            break;
        end
    end
    if error_found
        error('mod 错误');
    end
end

function test_builtin_rdivide
    a = [1.33,2,3;2,3.57,4.9;5,2.77,3.19];
    h = a./3;
    diff_div = h*3 - a;
    
    % 不使用 max 检查除法误差
    error_found = 0;
    for i = 1:numel(diff_div)
        if abs(diff_div(i)) > 1e-12
            error_found = 1;
            break;
        end
    end
    if error_found
        error('rdivide 错误');
    end
end

function test_builtin_and
    a = [1.33,2,3;2,3.57,4.9;5,2.77,3.19];
    if (2&3) ~= 1
        error('op_logic_and 错误');
    end
    if ~isequal((a>2) & (a<5), (a>2).*(a<5))
        error('op_logic_and 错误');
    end
end

function test_builtin_qr
    a = [1 -2 4 5; -5 2 0 8; 1 0 3 9 ;3 1 5 6];
    [Q,R,P] = qr(a);
    diff_qr = Q*R - a*P;
    
    % 不使用 max 检查 QR 误差
    error_found = 0;
    for i = 1:numel(diff_qr)
        if abs(diff_qr(i)) > 1e-12
            error_found = 1;
            break;
        end
    end
    if error_found
        error('qr 错误');
    end
end

function test_builtin_lu
    a = [1 -2 4 5; -5 2 0 8; 1 0 3 9 ;3 1 5 6];
    [L,U,P] = lu(a);
    diff_lu = L*U - P*a;
    
    % 不使用 max 检查 LU 误差
    error_found = 0;
    for i = 1:numel(diff_lu)
        if abs(diff_lu(i)) > 1e-12
            error_found = 1;
            break;
        end
    end
    if error_found
        error('lu 错误');
    end
end

function test_builtin_det
    a = [1 -2 4 5; -5 2 0 8; 1 0 3 9 ;3 1 5 6];
    D = det(a);
    if 393 - D > 1e-8
        error('det 错误');
    end
end

function test_builtin_rank
    a = [1 -2 4 5; -5 2 0 8; 1 0 3 9 ;3 1 5 6];
    [L,U] = lu(a);
    % 计算每行是否全 0，用 diag(U*U.') 表示每行平方和
    row_norm2 = diag(U*U.');
    tol = 1e-12;
    k2 = 0;
    for i = 1:length(row_norm2)
        if (row_norm2(i) > tol)
             k2 = k2+1;
        end
    end
    if rank(a) ~= k2
        error('rank 错误');
    end
end