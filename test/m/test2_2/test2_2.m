% 动态矩阵维度

function ret = test2_2()
tmp = ceil(rand()*4) + 4;
sizes_old = [tmp tmp];
numel_old = tmp^2;

Ndim_new = ceil(rand())*3 + 3;
sizes_new = zeros(1, Ndim_new);
numel_new = 1;
for i = 1:Ndim_new
    sizes_new(i) = ceil(rand()*1.5) + 1;
    numel_new = numel_new * sizes_new(i);
end

sizes_old = [sizes_old numel_new];
sizes_new = [sizes_new numel_old];

ret = 0;
a = rand(sizes_old);
ret = ret + hash_mat(a);

a2 = rand(sizes_old);
ret = ret + hash_mat(a2);

a_mul_a2 = zeros(sizes_old);
for k = 1:sizes_old(3)
    a_mul_a2(:,:,k) = a(:,:,k) * a2(:,:,k);
end
ret = ret + hash_mat(a_mul_a2);

b = reshape(a_mul_a2, sizes_new);
ret = ret + hash_mat(b);

b2 = rand(sizes_new);
ret = ret + hash_mat(b2);

idim = ceil(numel(sizes_new)*rand());

b2_flip = flip(b, idim) + b2;
ret = ret + hash_mat(b2_flip);
if ret > 10
    ret = 0;
else
    error('1');
end
end

function ret = hash_mat(a)
ret = 0;
for i4 = 1:size(a, 4)
    for i3 = 1:size(a, 3)
        for i2 = 1:size(a, 2)
            for i1 = 1:size(a, 1)
                tmp = i1*1 + i2*2 + i3*3 + i4*4;
                ret = ret + tmp * a(i1,i2,i3,i4) + tmp;
            end
        end
    end
end
end
