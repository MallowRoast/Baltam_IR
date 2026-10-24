# baltam_ir

将 `.m` 文件解析并打印当前生成的 Non-SSA IR。CMake 目标名是 `baltam_ir_cli`，生成的可执行文件名是 `baltam_ir`。

## 构建

在项目根目录执行：

```bash
cmake -S . -B build
cmake --build build --target baltam_ir_cli
```

## 临时加入 PATH

仍在项目根目录时，把**包含可执行文件的 `build` 目录**加入当前 Bash 的 `PATH`：

```bash
export PATH="$(pwd)/build:$PATH"
command -v baltam_ir
```

`command -v` 应显示项目 `build/baltam_ir` 的绝对路径。此设置只对当前终端及其子进程生效，关闭终端后失效；无需修改 `~/.bashrc`。如果已经离开项目目录，可使用绝对路径：

```bash
export PATH="/home/zj/Desktop/Baltam_IR/build:$PATH"
```

## 使用

```bash
baltam_ir test/syntax/constant.m
baltam_ir /absolute/path/to/function.m
baltam_ir test/syntax/constant.m -o constant.ir
```

输入和 `-o` 指定的输出路径都可以是相对路径或绝对路径；相对路径基于运行命令时的工作目录。不指定 `-o` 时，IR 打印到终端。

输出以 `; ModulePath = '绝对路径'` 开头，随后按源码位置打印 `define @函数名(参数槽) -> (返回槽) { ... }` 函数块。脚本主体打印为 `script { ... }`，脚本内的局部函数随后打印。嵌套函数紧随词法父函数打印，并以 `; nested in @父函数名` 标注。每个函数开头的 `; stage:` 注释显示当前 IR 阶段，`; slots:` 注释列出该函数所有 slot 的编号和名称。尚未实现的语法会报告错误。

代码中可直接调用 `IRBuilder::build_file(path)` 获得共享的 `IRModule`。同一进程内，该接口先查找 `IRCache`；有效条目直接复用，未命中或失效时解析文件并注册新模块。跨世界的文件修改时间检查由 `CacheEntry` 完成，运行时需要在合适的执行边界推进世界计数。
