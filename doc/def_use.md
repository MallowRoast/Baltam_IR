# Baltam_IR DefUse 说明

## 当前背景

当前仓库的正式 IR 主线是：

`AST -> non-SSA IR -> verify -> print`

当前已经落地的相关基础设施包括：

- IR 容器定义在 [src/ir/ir.h](/home/zj/Desktop/Baltam_IR/src/ir/ir.h)
- CFGAnalysis 定义在 [src/analysis/cfg_analysis.h](/home/zj/Desktop/Baltam_IR/src/analysis/cfg_analysis.h)
- DefUse 定义在 [src/analysis/def_use.h](/home/zj/Desktop/Baltam_IR/src/analysis/def_use.h)
- 名字级 def/use 提取辅助定义在 [src/analysis/name_analysis_utils.h](/home/zj/Desktop/Baltam_IR/src/analysis/name_analysis_utils.h)

也就是说，当前 DefUse analysis 不是在 SSA use-def 链上工作，而是建立在：

- 已验证的显式 CFG
- non-SSA 节点里的名字读写关系

之上。

## DefUse 的基础概念

当前 DefUse 讨论的是：

- 某个名字在哪些节点被定义
- 某个名字在哪些节点被使用
- 某个名字在哪些基本块里出现过定义

这里的“名字”不是 SSA value id，而是：

- `NamedValue.name`

也就是说，第一版当前实现统一把：

- 源码变量名
- lowering 临时量名

都当作字符串键处理。

### 1. definition

当某个节点“写入”一个名字时，记为该名字的一个 definition。

例如：

```text
x = y
```

这里：

- `x` 是 definition
- `y` 是 use

### 2. use

当某个节点“读取”一个名字时，记为该名字的一个 use。

例如：

```text
z = x + y
```

这里：

- `x` 是 use
- `y` 是 use
- `z` 是 definition

### 3. def blocks

除了记录具体定义节点之外，当前 DefUse 还记录：

- 某个名字在哪些基本块里至少定义过一次

这层信息对于后续：

- iterated dominance frontier
- `BuildPrunedSSA`

都很直接，因为它们更关心：

- “名字从哪些块开始产生多个版本”

而不一定一开始就需要知道每个块里所有具体定义语句。

### 4. 当前分析域

第一版当前实现只分析：

- non-SSA 节点中的 `NamedValue.name`

当前不单独引入：

- SSA value id
- 类型信息
- 内存别名
- 属性级字段访问

## 它和 SSA 的关系

DefUse 在当前仓库里最直接的用途是：

- 统计某个名字的定义点集合
- 作为后续 `BuildPrunedSSA` 的输入

例如，若名字 `x` 在块：

- `B1`
- `B7`
- `B9`

都出现过定义，那么后续就可以从这些 def blocks 出发，结合：

- `DominanceFrontier`
- `Liveness`

去决定 `x` 的 phi 插入位置。

因此当前 DefUse 的职责是：

- 提供名字级 def/use / def-block 基础索引

而不是：

- 直接构建 use-def graph
- 直接做 reaching definitions
- 直接生成 SSA 名字版本

## 当前 DefUse 的定位

当前 DefUse 是：

- 建立在 `CFGAnalysis` 之上的函数级 analysis

虽然从“概念输入”上说，它主要关心节点里的名字读写关系，但当前实现仍然显式依赖
`CFGAnalysis`，原因是：

- 只统计入口可达子图
- 按 `reverse_postorder` 稳定输出结果顺序

它的职责是：

- 为每个名字收集定义节点列表
- 为每个名字收集使用节点列表
- 为每个名字收集定义块列表

它不负责：

- 计算块边界活跃信息
- 计算 reaching definitions
- 建立 SSA use-def graph
- 解释“最后一次定义”这样的流敏感语义

也就是说，当前 DefUse 的角色是：

- “提供名字到定义/使用位置的静态索引”

而不是：

- “直接提供流敏感数据流事实”

## 输入与前置条件

DefUse 的输入是：

- `Function`
- `CFGAnalysis::Result`

也就是说，它依赖：

- 结构合法的 CFG
- `reverse_postorder`
- `is_reachable(...)`

这些都已经由：

- verifier
- CFGAnalysis

提供好了。

当前实现里，DefUse 通过：

- `analysis_manager.get<CFGAnalysis>(function)`

显式获取这个前置 analysis。

## 当前文件布局

当前实现位于：

- [src/analysis/def_use.h](/home/zj/Desktop/Baltam_IR/src/analysis/def_use.h)
- [src/analysis/def_use.cpp](/home/zj/Desktop/Baltam_IR/src/analysis/def_use.cpp)

共用的名字提取辅助位于：

- [src/analysis/name_analysis_utils.h](/home/zj/Desktop/Baltam_IR/src/analysis/name_analysis_utils.h)

对应测试位于：

- [test/def_use_test.cpp](/home/zj/Desktop/Baltam_IR/test/def_use_test.cpp)

## 当前接口

当前接口如下：

```cpp
class DefUse {
public:
    struct Result {
        std::unordered_map<std::string, std::vector<const NonSSANode*>> defs_of_name;
        std::unordered_map<std::string, std::vector<const NonSSANode*>> uses_of_name;
        std::unordered_map<std::string, std::vector<const BasicBlock*>> def_blocks_of_name;

        const std::vector<const NonSSANode*>& definitions_of(const std::string& name) const;
        const std::vector<const NonSSANode*>& uses_of(const std::string& name) const;
        const std::vector<const BasicBlock*>& definition_blocks_of(const std::string& name) const;
    };

    Result run(Function& function, FunctionAnalysisManager& analysis_manager) const;
};
```

这里的设计原则是：

- 直接暴露名字到结果列表的映射
- 用 `const NonSSANode*` 保留“分析只读视图”
- 用 `const BasicBlock*` 直接表达定义块
- 查询接口对缺失名字返回空列表

## 每个成员的语义

### 1. `defs_of_name`

类型：

- `std::unordered_map<std::string, std::vector<const NonSSANode*>>`

含义：

- 某个名字对应的所有定义节点列表

约定：

- 只统计可达块里的节点
- 节点顺序按：
  - `reverse_postorder` 的块顺序
  - 同块内的程序顺序
- 只要某个名字至少定义过一次，它才会作为 key 出现

### 2. `uses_of_name`

类型：

- `std::unordered_map<std::string, std::vector<const NonSSANode*>>`

含义：

- 某个名字对应的所有使用节点列表

约定：

- 只统计可达块里的节点
- 节点顺序按：
  - `reverse_postorder` 的块顺序
  - 同块内的程序顺序
- 只要某个名字至少使用过一次，它才会作为 key 出现

### 3. `def_blocks_of_name`

类型：

- `std::unordered_map<std::string, std::vector<const BasicBlock*>>`

含义：

- 某个名字在哪些可达块里出现过定义

约定：

- 同一块内无论定义多少次，只记录一次
- 块列表顺序按 `reverse_postorder`
- 只要某个名字至少定义过一次，它才会作为 key 出现

### 4. `definitions_of(...)`

语义：

- 返回某个名字的定义节点列表

约定：

- 空字符串返回空列表
- 从未定义过的名字返回空列表

### 5. `uses_of(...)`

语义：

- 返回某个名字的使用节点列表

约定：

- 空字符串返回空列表
- 从未使用过的名字返回空列表

### 6. `definition_blocks_of(...)`

语义：

- 返回某个名字的定义块列表

约定：

- 空字符串返回空列表
- 从未定义过的名字返回空列表

## 当前分析规则

当前实现按 non-SSA 节点中的名字读写关系统计 def / use。

具体规则是：

### 会产生 definition 的节点

- `NumberNode::result()`
- `TextNode::result()`
- `AssignNode::dst()`
- `UnaryOpNode::result()`
- `BinOpNode::result()`
- `CallNode::outputs()`

### 会产生 use 的节点

- `AssignNode::src()`
- `UnaryOpNode::operand()`
- `BinOpNode::lhs()`
- `BinOpNode::rhs()`
- `CallNode::inputs()`
- `CondJumpNode::cond()`
- `ReturnNode::values()`

### 额外约定

- `CallNode` 为 `Indirect` 时，`callee()` 会被计入 use
- `CallNode` 为 `Direct` 时，`callee()` 只是函数名文本，不计入 use
- `JumpNode` 不产生名字 use / def
- 函数签名里的输入参数不是具体节点，因此不会在 `defs_of_name` 里自动出现

最后这一点很重要：

- “函数参数是进入函数时已经可用的名字”

和

- “当前函数体内有一个具体 definition 节点”

不是同一件事。

## 当前算法

当前实现采用最直接的一次线性扫描方案。

核心思路是：

- 按可达块的 `reverse_postorder` 遍历
- 再按块内程序顺序遍历节点
- 每遇到一个 use 就记到 `uses_of_name`
- 每遇到一个 def 就记到 `defs_of_name`
- 同时把对应块去重后记到 `def_blocks_of_name`

### Step 1

先获取：

- `cfg = analysis_manager.get<CFGAnalysis>(function)`

### Step 2

按 `cfg.reverse_postorder` 遍历每个可达块。

这样做的好处是：

- 自动跳过不可达块
- 结果顺序稳定

### Step 3

对每个块，按：

- `instructions()`
- `terminal()`

的顺序访问当前块所有节点。

### Step 4

对每个节点：

- 先枚举 use，并追加到 `uses_of_name[name]`
- 再枚举 def，并追加到 `defs_of_name[name]`

### Step 5

每次遇到某个名字在某块的定义时：

- 用一张去重集合判断该块是否已经记录过
- 若还没有，则把该块追加到 `def_blocks_of_name[name]`

这样可以保证：

- 同一块多次定义只记一个 def block
- block 顺序仍然稳定

## 为什么不做更复杂的流敏感分析

对于当前仓库阶段，DefUse 的首要目标是：

- 给 `BuildPrunedSSA` 提供定义点集合和使用点索引

这并不要求它一开始就支持：

- reaching definitions
- use-def 链回连
- definition-use graph
- kill/gen 传播

因此当前保留最直接的静态索引形式最合适，原因是：

- 实现短
- 语义清楚
- 容易验证
- 足够支撑后续 SSA 构建

## 复杂度

若：

- 可达基本块数为 `B`
- 可达节点总数为 `I`
- 名字出现总次数为 `N`

则当前 DefUse 的主成本可以理解为：

- 时间复杂度：`O(I + N)`
- 空间复杂度：与所有名字的 def/use 记录总量成正比

由于当前实现只做单次扫描，不做迭代，因此它是这批基础 analysis 里相对便宜的一项。

## 对不可达块的处理

当前实现和 CFGAnalysis / DominatorTree / DominanceFrontier / Liveness 保持一致：

- 只统计入口可达子图
- 不可达块里的节点不会出现在 `defs_of_name`
- 不可达块里的节点不会出现在 `uses_of_name`
- 不可达块不会出现在 `def_blocks_of_name`

## 与 pass manager 的失效关系

当前仓库已经有：

- [src/analysis/analysis_manager.h](/home/zj/Desktop/Baltam_IR/src/analysis/analysis_manager.h)
- [src/optimizer/pass_manager.h](/home/zj/Desktop/Baltam_IR/src/optimizer/pass_manager.h)

因此 DefUse 的缓存失效规则建议明确如下：

- 只要 pass 改了 CFG 形状，就不应保留 `DefUse`
- 只要 pass 改了任何节点的名字读写关系，也不应保留 `DefUse`

典型会导致失效的改动包括：

- 增加/删除 `BasicBlock`
- 修改 CFG 边，导致可达子图变化
- 增删普通指令
- 修改节点的输入名字或输出名字
- 把 direct call 改成 indirect call，或反过来

相反，如果 pass 只改：

- 源码位置信息
- 不影响名字文本的其他元数据

那么 `DefUse` 才可以继续复用。

## 当前测试覆盖

当前测试位于：

- [test/def_use_test.cpp](/home/zj/Desktop/Baltam_IR/test/def_use_test.cpp)

覆盖的场景包括：

### 1. 调用节点的 use / def 规则

验证：

- `CallNode::outputs()` 会形成 definition
- `CallNode::inputs()` 会形成 use
- indirect callee 名字会形成 use
- direct callee 函数名不会形成 use

### 2. 定义块去重与可达性过滤

验证：

- 同名在多个可达块里的定义都能被记录
- 同一块里不会重复记录 def block
- 不可达块里的定义和使用不会污染结果

## 和后续 analysis / SSA 的关系

当前 DefUse 是后续这些步骤的直接前置之一：

- `BuildPrunedSSA`
- iterated dominance frontier 的名字级驱动
- 简单 DCE
- 简单复制传播

其中最直接的用途是：

- 先从 `definition_blocks_of(name)` 取某个名字的定义块集合
- 再结合 `DominanceFrontier` 和 `Liveness`
- 决定该名字真正需要插入 phi 的位置

## 当前结论

当前仓库里的 DefUse 可以概括为：

- 输入：
  - `Function`
  - `CFGAnalysis::Result`
- 输出：
  - `name -> definition nodes`
  - `name -> use nodes`
  - `name -> definition blocks`
- 范围：
  - 只覆盖入口可达子图
- 分析域：
  - `NamedValue.name`
- 作用：
  - 为后续 `BuildPrunedSSA` 和名字级优化提供定义/使用索引

它是从块级结构 analysis 过渡到名字级 SSA 构建时的另一块核心基础设施。
