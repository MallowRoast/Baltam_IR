# `runtime_private_plus_dispatch_probe` 观察记录

本文记录 `runtime_private_plus_dispatch_probe.m` 的可观测现象，并给出一个足够解释这些现象的
工作模型；这里不试图声称已经完整还原 MATLAB 的内部实现。

## 1. 脚本概览

这个 probe 只做一件事：在同一个临时函数体里，同时观察

- `1 + 2`
- `plus(1, 2)`

然后比较在 `private/plus.m` 运行期新增或删除之后，不同操作对这两个调用点分派结果的影响。

### 1.1 两类场景

脚本固定测两类场景：

1. 初始没有 `private/plus.m`，运行期新增 `private/plus.m`
2. 初始存在 `private/plus.m`，运行期删除 `private/plus.m`

### 1.2 八种操作

每一类场景都测 8 种操作：

1. 不做任何事
2. 只做 `clear plus`
3. 只做 `clear functions`
4. 只做 `rehash`
5. 先 `clear plus` 再 `rehash`
6. 先 `rehash` 再 `clear plus`
7. 先 `clear functions` 再 `rehash`
8. 先 `rehash` 再 `clear functions`

### 1.3 脚本结构

脚本结构可以压成 4 个部分：

- `runtime_private_plus_dispatch_probe()`
  依次跑 “add private” 与 “delete private” 两组 case。
- `run_add_case(mode)` / `run_delete_case(mode)`
  在临时目录里构造对应场景，执行指定操作，然后调用 `probe_plus_dispatch()` 看结果。
- `setup_root(with_private)`
  创建临时目录、生成 `probe_plus_dispatch.m`，并按需要预置 `private/plus.m`。
- `teardown_root(root)`
  还原 path 并删除临时目录。

### 1.4 生成的 `probe_plus_dispatch`

真正被观察的函数体是脚本临时生成的 `probe_plus_dispatch.m`，核心逻辑只有三行：

```matlab
disp(class(1));
try, disp(1 + 2); catch ME, disp(['1+2 ERROR: ' ME.message]); end
try, disp(plus(1, 2)); catch ME, disp(['plus ERROR: ' ME.message]); end
```

其中：

- `class(1)` 用来确认操作数类型始终还是 `double`
- `1 + 2` 用来观察函数体内部运算符分派
- `plus(1, 2)` 用来观察普通函数名调用分派

### 1.5 完整脚本

```matlab
function runtime_private_plus_dispatch_probe()
% 验证 add/delete private 后，rehash 与 clear functions 的组合影响。

disp("=== add private ===");
run_add_case("none");
run_add_case("clear_plus");
run_add_case("clear");
run_add_case("rehash");
run_add_case("clear_plus_rehash");
run_add_case("rehash_clear_plus");
run_add_case("clear_rehash");
run_add_case("rehash_clear");

disp("=== delete private ===");
run_delete_case("none");
run_delete_case("clear_plus");
run_delete_case("clear");
run_delete_case("rehash");
run_delete_case("clear_plus_rehash");
run_delete_case("rehash_clear_plus");
run_delete_case("clear_rehash");
run_delete_case("rehash_clear");
end

function run_add_case(mode)
root = setup_root(false);

private_dir = fullfile(root, "private");
private_plus = fullfile(private_dir, "plus.m");

disp("--- mode: " + mode + " ---");
probe_plus_dispatch();
ensure_private_plus(private_dir, private_plus);

switch mode
    case "clear_plus"
        clear plus;
    case "clear"
        clear functions;
    case "rehash"
        rehash;
    case "clear_plus_rehash"
        clear plus;
        rehash;
    case "rehash_clear_plus"
        rehash;
        clear plus;
    case "clear_rehash"
        clear functions;
        rehash;
    case "rehash_clear"
        rehash;
        clear functions;
end

probe_plus_dispatch();
teardown_root(root);
end

function run_delete_case(mode)
root = setup_root(true);

private_plus = fullfile(root, "private", "plus.m");

disp("--- mode: " + mode + " ---");
probe_plus_dispatch();
delete(private_plus);

switch mode
    case "clear_plus"
        clear plus;
    case "clear"
        clear functions;
    case "rehash"
        rehash;
    case "clear_plus_rehash"
        clear plus;
        rehash;
    case "rehash_clear_plus"
        rehash;
        clear plus;
    case "clear_rehash"
        clear functions;
        rehash;
    case "rehash_clear"
        rehash;
        clear functions;
end

probe_plus_dispatch();
teardown_root(root);
end

function root = setup_root(with_private)
root = tempname();
mkdir(root);

probe_file = fullfile(root, "probe_plus_dispatch.m");
fid = fopen(probe_file, "w");
fprintf(fid, "function probe_plus_dispatch()\n");
fprintf(fid, "disp(class(1));\n");
fprintf(fid, "try, disp(1 + 2); catch ME, disp(['1+2 ERROR: ' ME.message]); end\n");
fprintf(fid, "try, disp(plus(1, 2)); catch ME, disp(['plus ERROR: ' ME.message]); end\n");
fprintf(fid, "end\n");
fclose(fid);

if with_private
    ensure_private_plus(fullfile(root, "private"), fullfile(root, "private", "plus.m"));
end

restoredefaultpath;
addpath(root);
rehash;
end

function ensure_private_plus(private_dir, private_plus)
if exist(private_dir, "dir") ~= 7
    mkdir(private_dir);
end
fid = fopen(private_plus, "w");
fprintf(fid, "function z = plus(~, ~)\n");
fprintf(fid, "z = 901;\n");
fprintf(fid, "end\n");
fclose(fid);
end

function teardown_root(root)
restoredefaultpath;
if exist(root, "dir") == 7
    rmdir(root, "s");
end
end
```

## 2. 观测结果

所有场景里 `class(1)` 都保持 `double`，说明变化来自分派目标，不是操作数类型变化。

### 2.1 新增 `private/plus.m`

| 操作 | `1 + 2` | `plus(1, 2)` |
|---|---:|---:|
| 不做任何事 | `3` | `3` |
| `clear plus` | `3` | `3` |
| `clear functions` | `3` | `3` |
| `rehash` | `3` | `901` |
| `clear plus` + `rehash` | `3` | `3` |
| `rehash` + `clear plus` | `3` | `901` |
| `clear functions` + `rehash` | `901` | `901` |
| `rehash` + `clear functions` | `901` | `901` |

### 2.2 删除 `private/plus.m`

| 操作 | `1 + 2` | `plus(1, 2)` |
|---|---:|---:|
| 不做任何事 | `901` | `inaccessible error` |
| `clear plus` | `901` | `inaccessible error` |
| `clear functions` | `inaccessible error` | `inaccessible error` |
| `rehash` | `901` | `3` |
| `clear plus` + `rehash` | `901` | `3` |
| `rehash` + `clear plus` | `901` | `3` |
| `clear functions` + `rehash` | `3` | `3` |
| `rehash` + `clear functions` | `3` | `3` |

### 2.3 可直接使用的结论

- 只 `rehash` 不 `clear functions` 时，`plus(1, 2)` 会变，但函数体里的 `1 + 2` 不会变。
- `clear plus` 不会让函数体里的 `1 + 2` 重新分派。
- 单独 `clear functions` 会让 `1 + 2` 所在函数体重新加载，但若没有同时刷新文件可见性，它仍可能按旧的 `private` 状态重新建立分派绑定。
- `clear functions + rehash` 和 `rehash + clear functions` 在这个 probe 里最终结果一致：两者都会收敛到新的可见分派结果。

## 3. 补充观察

### 3.1 `clear plus` 与 `rehash` 的顺序

补充 probe：`add_private_clear_plus_order_probe.m`。

新增 `private/plus.m` 之后，针对 `plus(1, 2)`：

- `rehash`：`901`
- `clear plus; rehash;`：`3`
- `rehash; clear plus;`：`901`

这说明：

- `clear plus` 放在 `rehash` 之前，会影响后续 `rehash` 建立新 private 绑定的结果。
- `clear plus` 放在 `rehash` 之后，不会把刚刚通过 `rehash` 建立的 `plus(1, 2)` 分派再清掉。
- 这个顺序差异只出现在 `plus(1, 2)` 这一层，不会影响函数体里的 `1 + 2`。

#### 3.1.1 完整脚本

```matlab
function add_private_clear_plus_order_probe()
% 聚焦验证 add private 后，clear plus 与 rehash 的先后顺序影响。

run_case("rehash_only");
run_case("clear_plus_rehash");
run_case("rehash_clear_plus");
end

function run_case(mode)
root = tempname();
mkdir(root);

probe_file = fullfile(root, "probe_plus_dispatch.m");
private_dir = fullfile(root, "private");
private_plus = fullfile(private_dir, "plus.m");

fid = fopen(probe_file, "w");
fprintf(fid, "function probe_plus_dispatch()\n");
fprintf(fid, "try, disp(1 + 2); catch ME, disp(['1+2 ERROR: ' ME.message]); end\n");
fprintf(fid, "try, disp(plus(1, 2)); catch ME, disp(['plus ERROR: ' ME.message]); end\n");
fprintf(fid, "end\n");
fclose(fid);

restoredefaultpath;
addpath(root);
rehash;

disp("=== mode: " + mode + " ===");
disp("--- baseline ---");
probe_plus_dispatch();

mkdir(private_dir);
fid = fopen(private_plus, "w");
fprintf(fid, "function z = plus(~, ~)\n");
fprintf(fid, "z = 901;\n");
fprintf(fid, "end\n");
fclose(fid);

switch mode
    case "rehash_only"
        rehash;
    case "clear_plus_rehash"
        clear plus;
        rehash;
    case "rehash_clear_plus"
        rehash;
        clear plus;
end

disp("--- after add private ---");
probe_plus_dispatch();

restoredefaultpath;
if exist(root, "dir") == 7
    rmdir(root, "s");
end
end
```

### 3.2 `inaccessible error` 的抛出阶段

补充 probe：`inaccessible_error_stage_probe.m`。

这个 probe 先让 `caller/private/foo.m` 返回 `901`，同时在 path 上放一个 `base/foo.m` 返回 `101`，然后删除 `private/foo.m`：

- baseline with private `foo`：`901`
- delete private 后，不 `rehash`：`inaccessible error`
- delete private 后，只 `rehash`：仍然是 `inaccessible error`
- delete private 后，`clear functions + rehash`：`101`

这说明：

- `inaccessible error` 不是“重新做了一轮候选分派后失败”才抛出。
- 更像是“当前调用点先拿旧绑定去调用，发现之前可访问的文件现在不可访问，于是直接在这里报错”。
- 只有在旧绑定被真正丢弃后，MATLAB 才会重新分派到其他候选函数。

#### 3.2.1 完整脚本

```matlab
function inaccessible_error_stage_probe()
% 验证删除已命中的 private 函数后，MATLAB 是先报 inaccessible，
% 还是会回退到其他候选函数分派。

root = tempname();
base_dir = fullfile(root, "base");
caller_dir = fullfile(root, "caller");
private_dir = fullfile(caller_dir, "private");
mkdir(root);
mkdir(base_dir);
mkdir(caller_dir);
mkdir(private_dir);

write_foo(fullfile(base_dir, "foo.m"), 101);
write_foo(fullfile(private_dir, "foo.m"), 901);
write_caller(fullfile(caller_dir, "caller.m"));

restoredefaultpath;
addpath(base_dir);
addpath(caller_dir);
rehash;

disp("=== baseline with private foo ===");
caller();

delete(fullfile(private_dir, "foo.m"));

disp("=== after delete private foo, no rehash ===");
caller();

rehash;
disp("=== after delete private foo, with rehash ===");
caller();

clear functions;
rehash;
disp("=== after delete private foo, clear functions + rehash ===");
caller();

restoredefaultpath;
if exist(root, "dir") == 7
    rmdir(root, "s");
end
end

function write_foo(filename, value)
fid = fopen(filename, "w");
fprintf(fid, "function y = foo(~)\n");
fprintf(fid, "y = %d;\n", value);
fprintf(fid, "end\n");
fclose(fid);
end

function write_caller(filename)
fid = fopen(filename, "w");
fprintf(fid, "function caller()\n");
fprintf(fid, "try, disp(foo(1)); catch ME, disp(['foo ERROR: ' ME.message]); end\n");
fprintf(fid, "end\n");
fclose(fid);
end
```

## 4. 一个足够解释现象的工作模型

一个足够解释这些现象的工作模型是：

1. `plus(1, 2)` 更接近每次调用时的普通函数名解析，但它的分派结果也会被缓存。
2. 函数体中的 `1 + 2` 更接近函数体加载时已经收敛好的运算符分派结果。
3. `rehash` 更接近刷新 MATLAB 对文件系统与函数可见性的认知。
4. `clear functions` 更接近清掉已经加载的函数体，以及它们附带的分派缓存。
5. `clear plus` 只会影响 `plus` 这个函数对象本身，不会让函数体里的运算符表达式重新分派，也不会替代 `rehash`。

据此可以解释前面的矩阵：

- 新增 `private/plus.m` 后，只做 `rehash`，`plus(1, 2)` 会先看到新 private；但 `1 + 2` 所在函数体没有重新加载，所以仍沿用旧分派。
- 新增 `private/plus.m` 后，只做 `clear plus` 不会生效；这说明 `clear plus` 不会刷新 MATLAB 对新增文件的可见性认知。
- 新增 `private/plus.m` 后，只做 `clear functions` 不足以让新 private 生效；虽然函数体会重装，但它仍按旧的文件可见性重新绑定，所以 `1 + 2` 还是旧结果。
- 删除 `private/plus.m` 后，只做 `rehash`，`plus(1, 2)` 会先回到数值 `plus`；但 `1 + 2` 仍沿用旧分派。
- 删除 `private/plus.m` 后，只做 `clear plus` 也不会让 `plus(1, 2)` 回到数值 `plus`；它仍会沿用之前命中的 private `plus`，直到发现该文件已不可访问。
- 当 `rehash` 与 `clear functions` 同时发生时，不论先后顺序如何，`plus(1, 2)` 与 `1 + 2` 最终都会收敛到新的可见分派结果；前者刷新文件可见性，后者丢弃旧函数体里的调用点绑定。
