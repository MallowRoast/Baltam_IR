# CFG DOT 输出设计

## 目标

`cfg_dot` 用于把 IR 中的 `CodeUnit / BasicBlock / terminator` 关系打印成
Graphviz DOT，方便直接观察控制流结构。

文本 IR 主要用于查看指令细节、`ValueId`、slot、source line 和 internal helper；
DOT 输出主要用于查看基本块连接关系，例如：

- `if` 的 true / false 分支是否接到正确 block
- `for / while` 的 header、body、latch、end 是否连通
- `break / continue` 是否跳到最近一层循环的目标块
- 嵌套循环中重复 block label 是否能区分

当前实现更接近 LLVM 的 `dot-cfg-only`：节点只显示 block 名和摘要信息，不展开完整
IR 指令。后续如果需要对齐 LLVM 的 `dot-cfg`，可以在 `CFGDotOptions` 中增加节点详细
程度选项，把 block 内指令也渲染进节点；这个扩展点目前保留为源码中的 TODO。

## 代码位置

- 头文件：[cfg_dot.h](/home/zj/Desktop/Baltam_IR/tools/cfg_dot.h)
- 实现文件：[cfg_dot.cpp](/home/zj/Desktop/Baltam_IR/tools/cfg_dot.cpp)
- 命令行工具：[cfg_dot_main.cpp](/home/zj/Desktop/Baltam_IR/tools/cfg_dot_main.cpp)
- smoke test：[cfg_dot_smoke.cpp](/home/zj/Desktop/Baltam_IR/test/smoke_test/feature/cfg_dot_smoke.cpp)

公开接口：

```cpp
std::string format_cfg_dot(const CodeUnit& unit, const CFGDotOptions& options = {});
std::string format_cfg_dot(const MFileUnit& mfile, const CFGDotOptions& options = {});

void print_cfg_dot(std::ostream& os, const CodeUnit& unit, const CFGDotOptions& options = {});
void print_cfg_dot(std::ostream& os, const MFileUnit& mfile, const CFGDotOptions& options = {});
```

## 输出粒度

`format_cfg_dot(const MFileUnit&)` 输出一个 `digraph`：

- 图名使用 `.m` 文件 stem
- 每个 `CodeUnit` 输出为一个 `subgraph cluster`
- `cluster` label 使用 `CodeUnit::name`
- `BasicBlock` 输出为 DOT 节点
- terminator 输出为 DOT 边

多数语法测试是单脚本单元；local function 用例会在同一个 DOT 中出现多个
`CodeUnit` cluster。

## 节点设计

节点展示名复用文本 IR 的规则：

- 使用 `BasicBlock::label`
- 同一 `CodeUnit` 内重复 label 自动加 `.1 / .2 ...` 后缀
- 空 label 使用 `bbN`

节点默认打印非 terminator 指令数量：

```dot
u0_bb_9 [label="while.header\ninsts: 3", fillcolor="#eaf7ea"];
```

节点颜色按 block label 前缀区分：

- `entry`：灰色
- `for.*`：浅蓝
- `while.*`：浅绿
- `if.*`：浅黄
- 其它 block：白色

## 边设计

边直接来自 block terminator，而不是单独遍历 `BasicBlock::successors`。这样可以保留
terminator 的语义信息：

- `GotoInst` 输出一条边
- `BranchInst` 输出两条边，分别标注 `true / false`
- `ReturnInst` 不输出后继边

用户级 `break / continue` 由 lowering 生成非 synthetic `GotoInst`。DOT printer 根据
目标 block 类别标注：

- 跳到 `for.end / while.end`：`break`
- 跳到 `for.latch / while.latch`：`continue`

普通 synthetic 跳转不标注，避免图中过多噪声。

## 测试 DOT 文件

当前为 `test/m` 下的用例生成对应 DOT 文件，放在 build 目录：

```text
build/test/cfg_dot/
```

目录结构与 `test/m` 保持一致，例如：

```text
test/m/test3/test3_3.m
build/test/cfg_dot/test3/test3_3.dot
```

这些文件是构建产物，用于人工查看或渲染，不作为 verifier 的唯一依据。结构正确性仍由
`ir_verify` 和 smoke test 覆盖。

生成全部测试 DOT：

```bash
cmake --build build --target generate_cfg_dot
```

也可以直接使用命令行工具生成单个文件：

```bash
./build/ir_cfg_dot test/m/test3/test3_3.m -o build/test/cfg_dot/test3/test3_3.dot
```

如果本机安装了 Graphviz，可以同时生成图片：

```bash
./build/ir_cfg_dot test/m/test3/test3_3.m \
  -o build/test/cfg_dot/test3/test3_3.dot \
  --svg /tmp/test3_3.svg \
  --png /tmp/test3_3.png
```

## 后续扩展

- 增加 `CFGDotNodeDetail`，支持 `StructureOnly` 与 `WithInstructions`
- 支持把 source line 或源码片段放到 node / edge tooltip
- 支持按 loop region 输出 subgraph cluster
