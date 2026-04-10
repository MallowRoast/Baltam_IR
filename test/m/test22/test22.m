% ODE 求解器

function test22
g = 9.81; % 重力加速度 (m/s²)
v0 = 100; % 初始速度 (m/s)
theta = deg2rad(45); % 发射仰角 (弧度)
% 初始条件 [x, vx, y, vy]
init_cond = [0, v0*cos(theta), 0, v0*sin(theta)];
% 时间范围 (0到10秒)
tspan = [0 10];
% 求解ODE
[t, sol] = ode45(@projectile, tspan, init_cond);

if t(end) ~= 10
    error('1');
end
if max(abs(sol(end,:) - [707.1068   70.7107  216.6068  -27.3893])) > 5e-5
    error('2');
end
disp('末位置 (x, y):'); disp(sol(end,[1,3]));
disp('末速度 (vx, vy):'); disp(sol(end,[2,4]));

% 可视化轨迹
% plot(sol(:,1), sol(:,3));
% xlabel('水平距离 (m)');
% ylabel('高度 (m)');
% title('弹道轨迹');
end

function dydt = projectile(t, y)
g = 9.81;
dydt = [y(2); 0; y(4); -g]; % 忽略空气阻力
end
