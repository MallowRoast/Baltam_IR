# Baltam_IR SSA 方案整理

## 目标

本文档整理 Baltam_IR 后续引入 SSA 的建议方案，重点回答下面几个问题：

- 当前 IR 是否值得 SSA 化
- SSA 应该在什么阶段引入
- SSA 化应以什么形式落到现有代码结构中
- 在 SSA 之前和之后，哪些优化 pass 最适合实现
- 如何控制改造风险，避免一次性重写解释器和 lowering

本文档面向当前仓库已有实现：

- `AST -> IR lowering`
- IR 文本打印
- IR 解释执行
- 显式 `BasicBlock + Jump/CondJump/Return`

并将 SSA 视为“优化 IR 的演进方向”，而不是要求立刻把整个系统一次性改写成 SSA。

## 当前判断

### 1. 当前 IR 暂时还能继续工作，但已经开始接近 SSA 改造点

当前 IR 已经具备几个很重要的前提：

- 显式 CFG
- 显式基本块
- 显式终结指令
- 指令级源码位置信息

这些基础设施已经足够支撑后续做 dominator、phi 插入、rename 和 def-use 分析。

但当前 IR 的数据流模型仍然是“按名字读写变量”，典型特征包括：

- `AssignInstruction(name, value)` 直接写变量名
- `NameInstruction(name)` 直接读变量名
- `CallInstruction` 的输出绑定仍然保留源码名字语义
- 解释器执行时按 frame 中的名字表做读写

这意味着当前 IR 仍然是“变量可多次赋值”的非 SSA 形式。

### 2. 如果只做 very early passes，可以先不强制 SSA

以下优化即使在非 SSA 形式下也可以先做：

- 局部常量折叠
- 单基本块内常量传播
- 可达块清理
- 很保守的死代码消除

所以从“短期交付最小可用优化器”的角度看，不必为了这些 pass 立刻重写 IR。

### 3. 如果准备继续做全局优化，SSA 很快会变得值得

一旦后续目标包括：

- 跨基本块常量传播
- 分支合流后的数据流分析
- 更可靠的死代码消除
- 稀疏条件常量传播
- 循环相关优化
- 为未来 JIT 或专门化 lowering 准备更干净的优化输入

那么 SSA 会明显降低实现复杂度。

结论可以概括为：

- 现在不必把“执行 IR”立刻全部 SSA 化
- 但应尽快规划“优化 IR”向 SSA 演进
- 不建议等到引入 JIT 之后再开始 SSA 化

### 4. 当前仓库已经进入 value-based 迁移阶段

和最初只有“名字环境 IR”的状态相比，当前仓库里已经有一批 SSA 前置基础设施落地：

- `Instruction` 已经支持 `ValueId` / `ValueRef` / `InstValue`
- 解释器的 `Frame` 同时维护名字表和 value 槽表
- 大多数产值指令在 lowering 阶段就会拿到稳定的 `ValueId`
- `UnaryOp`、`BinOp`、`Assign`、`Call`、`CondJump`、`Return` 都已经能显式携带 `ValueRef`
- `CallInstruction`、`ReturnInstruction` 以及若干核心表达式节点已经支持 ref-first 构造
- 解释器和打印器已经优先通过 ref-first 访问 API 读取调用输入、返回值、条件和表达式依赖
- `UnaryOp`、`BinOp`、`Assign`、`CondJump`、`Return` 这些核心节点已经完全转到 `ValueRef` 主路径
- `CallInstruction` 的输入已经收敛成纯 `ValueRef` 列表；调用输出绑定也已经从调用节点内部迁移为 lowering 显式生成的 `AssignInstruction`
- 新 lower 出来的 `UnaryOp`、`BinOp`、`Assign`、`CondJump` 都不再保存兼容操作数指针
- 解释器在 value 槽还没写入时，已经可以通过 `ValueId -> 定义指令` 回溯物化对应值
- `ReturnInstruction` 已经收敛成纯 `ValueRef` 返回列表；lowering、解释器和打印器都不再依赖显式返回值指针列表

这意味着本文档下面提到的“Phase 1：把当前 IR 推向 value-based”不再只是设计建议，而是已经在持续进行中的代码迁移。

## 为什么 SSA 值得做

## 数据流更显式

在 SSA 中，每个定义只写一次，合流点通过 `phi` 显式表达。这样可以避免很多“这个名字当前到底对应哪次赋值”的隐式推理。

对当前项目最直接的好处是：

- 常量传播更容易做成全局分析
- DCE 可以基于 def-use 链而不是变量名回溯
- 冗余赋值和未使用中间值更容易识别
- 分支与循环的合流行为更容易建模

## 更适合作为优化 IR

SSA 的价值主要体现在“分析与优化”，不在于解释器是否必须直接执行 SSA。

因此，对 Baltam_IR 更合理的定位是：

- 非 SSA IR：更贴近当前 lowering 和解释执行
- SSA IR：优化用 IR，服务于全局数据流分析和后续专门化

这也意味着当前解释器不一定要第一时间重写成“直接执行 SSA IR”。

## 更适合后续 JIT

SSA 不是 JIT 专属技术，但 JIT 往往更喜欢输入是已整理过的数据流 IR。提前把优化层 SSA 化，有几个长期好处：

- 热点分析后更容易做 specialized lowering
- 更容易建立值版本、guard 和去装箱路径
- 后续降到更低层 IR 或机器码时边界更清晰

## 当前 IR 的主要约束

## 1. 值与变量只完成了部分分离

当前模型里，表达式结果与变量存储还混在一起：

- `NumberInstruction`、`BinOpInstruction`、`UnaryOpInstruction` 是值节点
- `AssignInstruction` 又直接把值写回名字
- `NameInstruction` 主要仍承担变量读取角色

相比最初状态，这一层已经有明显进展：

- 产值指令开始拥有显式 `ValueId`
- 用值位置开始显式记录 `ValueRef`
- 解释器已经能按 `ValueRef` 读取运行时值

但整体上仍然没有彻底分离，因为：

- `AssignInstruction` 仍然保留源码名字绑定语义
- `NameInstruction` 仍然同时承担“按名字读取变量”的旧职责
- 一部分节点仍保留 `Instruction*` 作为兼容字段

因此，这一阶段更准确地说是“near-SSA / value-based 迁移中”，还不是纯 value IR。

更适合 SSA 的方向是：

- 把“值定义”与“变量存储”区分开
- 把“读取变量当前版本”与“写入变量新版本”显式化
- 让每条可产出结果的指令有稳定的结果值语义

## 2. 调用输出仍保留源码层绑定语义

MATLAB-like 语言的多返回值调用是当前 SSA 改造中的关键点。

当前 `CallInstruction` 仍然保留：

- `name`
- `output_count`
- `in_arg_refs`

但相比最初状态，当前实现已经前进了一步：

- `CallInstruction` 本身已经可以定义多个结果值
- 输入参数已经显式记录为 `ValueRef`
- 输出绑定已经移出 `CallInstruction` 本体，改由 lowering 生成显式 `AssignInstruction`
- 构造入口已经可以走 ref-first 路径

当前还保留的非 SSA 部分，主要是“调用后按名字绑定结果”的语义本身，而不是它的表示形式。

要进入 SSA，这里迟早要演进成下面两种形态之一：

### 方案 A：调用返回多个 SSA 结果

例如逻辑上写成：

- `%r0, %r1 = call foo(%a, %b)`

再由后续名字绑定或 destructuring 把它们接到局部变量版本。

### 方案 B：调用返回一个 tuple-like 结果，再显式解包

例如逻辑上写成：

- `%call = call foo(%a, %b)`
- `%r0 = extract_result %call, 0`
- `%r1 = extract_result %call, 1`

对当前项目来说，方案 A 更直接，也更贴近现有多输出调用语义。

## 3. 解释器当前是“名字环境 + value 槽位”双轨执行

当前解释器仍然保留名字环境语义：

- 读变量：按名字查 frame
- 写变量：按名字回写 frame

但它已经不再是纯名字模型，还新增了：

- 每条产值指令执行后会把结果写入 `ValueId` 槽位
- `UnaryOp`、`BinOp`、`Assign`、`Call`、`CondJump`、`Return` 等位置会优先按 `ValueRef` 取值
- 线性解释执行会主动物化普通产值指令，而不是所有值都靠递归回退
- 当某个 `ValueRef` 对应的槽位尚未写入时，解释器已经能按 `ValueId` 找到定义指令并补做物化

这与 SSA 的寄存器式值模型仍然不同，但已经明显更接近 value-based IR 解释器。

因此改造时需要明确一个原则：

- 不要把“SSA 化”与“解释器重写”绑成同一次大改

更稳妥的做法是先让 SSA 成为优化层，再在 SSA 之后做降级或重写回可执行 IR。

## 推荐总体架构

推荐把 IR 分成两层，而不是只保留一层：

### 1. 执行 IR

职责：

- 承接现有 lowering
- 保持较强源码可读性
- 继续服务于解释器执行
- 作为 fallback IR 保留动态语义

特点：

- 可非 SSA
- 允许按变量名建模
- 更贴近源码工作区语义

### 2. 优化 IR

职责：

- 进行常量折叠
- 进行常量传播
- 进行 DCE
- 做 CFG 简化
- 为未来热点专门化/JIT 提供更规整输入

特点：

- 建议做 SSA
- 更偏 value-based IR
- 显式 `phi`
- 显式 def-use

推荐主线：

`AST -> 执行 IR -> SSA 优化 IR -> 优化 passes -> 回写/降级到执行 IR 或继续 lower 到后端`

在项目当前阶段，也可以先只做到：

`AST -> 执行 IR -> SSA 优化 IR -> 打印/验证`

先把基础设施搭起来，再决定是否把优化结果重新喂给解释器。

## SSA 方案设计

## 1. SSA 的适用范围

第一阶段建议只覆盖：

- 函数内局部变量
- 形参
- 返回值名
- lowering 过程中生成的隐藏临时变量

第一阶段不建议急着 SSA 化下面这些潜在更动态的实体：

- 全局变量语义
- `eval` 一类动态名字注入
- `assignin` / `evalin` 风格行为
- 复杂对象属性写入
- 动态索引写回的别名问题

也就是说，第一阶段目标应是：

- 在“当前已能稳定 lower 的局部函数/脚本子集”上建立 SSA

## 2. SSA 的基本对象

建议引入下面几个概念。

### ValueId

每个 SSA 值一个 `ValueId`，代表一条定义产生的结果。

典型来源：

- 常量
- 二元运算
- 一元运算
- 调用结果
- `phi`
- 参数

### LocalName

保留源码层名字，主要用于：

- debug 打印
- 源码映射
- rename 时追踪“这个 SSA 版本原来属于哪个源码名字”

### PhiInstruction

在控制流合流块显式引入：

- `%x3 = phi [bb1: %x1], [bb2: %x2]`

phi 不表示运行时真正执行的普通算术，而是控制流相关的值选择。

当前仓库已经落了一版可执行的 `PhiInstruction`：

- 解释器会在 block 入口按前驱块先解析 phi
- lowering 已经开始在 `if` / `switch` 的 merge block 生成 phi
- 当前生成范围是“各分支都赋值的名字”，先避免未定义名字快照带来的语义回归

### BlockArgument 备选

如果后续更想走现代 IR 风格，也可以把 `phi` 进一步抽象成 block arguments。

但对当前代码库来说，第一阶段直接引入 `PhiInstruction` 更容易落地，也更容易和现有 `BasicBlock` 结构兼容。

## 3. 先转“近 SSA”，再转完整 SSA

建议不要一步到位直接彻底重构所有 IR 节点，而是分两步。

### 第一步：Value-based 非 SSA IR

目标：

- 把“值定义”和“变量名”分离
- 让大多数表达式与调用先变成结果值导向

这一步建议的调整包括：

- 让算术/比较/一元运算天然产出 SSA 候选结果
- 让 `CallInstruction` 产出结果值，而不是直接写 `out_args`
- 把 `AssignInstruction` 收缩成“名字绑定”或“store local”语义

这一阶段即使还没 `phi`，也会比现在更适合后续做 SSA rename。

### 第二步：插入 phi，完成 SSA rename

在已有 CFG 基础上补齐：

- dominator tree
- dominance frontier
- phi 插入
- rename pass

完成之后，局部变量引用逐步由“按名字查当前值”变成“直接引用具体 SSA 定义”。

## 4. 参数、返回值与调用的处理

### 参数

函数参数天然可视为 SSA 定义。

建议：

- 每个输入参数在入口块都有一个初始 SSA 值
- 打印时保留源码参数名作为注释或符号名

### 返回值

MATLAB-like 语言经常通过“给输出变量名赋值”来形成返回值。

第一阶段建议不要急着改变源码语义，而是：

- 返回值名在 IR 中先视为普通局部名字
- 函数结束时，根据当前版本读取这些名字对应的 SSA 值
- `ReturnInstruction` 显式携带返回 SSA 值列表

这样更利于优化分析，也更接近后续后端接口。

### 多返回值调用

推荐第一阶段把 `CallInstruction` 改造成：

- `callee`
- `in_args`
- `value_count`

并让调用返回若干 SSA 结果值。

若当前实现更喜欢保持显式对象关系，也可以让 `CallInstruction` 本身作为“多结果定义”，由外部索引每个结果。

关键点不是最终语法细节，而是：

- 调用输出不应继续建模成“按名字写回”

## 5. 名字与调试信息

SSA 化之后，源码名字不能丢。

建议每个 SSA 定义至少保留：

- 原始名字，如 `x`
- SSA 版本号，如 `x.3`
- 源码位置
- 可选的类型/profile 注释

文本 IR 打印时建议显示：

- `%x.1 = const 1`
- `%x.2 = add %x.1, %y.0`
- `%x.3 = phi [then: %x.1], [else: %x.2]`

这样既保留调试可读性，也不会失去源码映射。

## 需要新增的基础设施

## 1. CFG 分析基础设施

SSA 化前应补齐：

- 反向后继/前驱遍历接口
- 可达块分析
- dominator 计算
- dominance frontier 计算

虽然当前 `BasicBlock` 已经有前驱和后继，但还缺“分析结果对象”这一层。

建议新增 `analysis/` 或 `optimizer/analysis/` 目录，集中放：

- `cfg_analysis`
- `dom_tree`
- `dom_frontier`
- `use_def_analysis`

## 2. 指令副作用建模

做 DCE 和代码移动前，需要先明确哪些指令可能有副作用。

建议先做一个保守分类：

- 纯计算：`Number`、`UnaryOp`、`BinOp`
- 纯 SSA 合流：`Phi`
- 可能有副作用：大多数 `Call`
- 控制流终结：`CondJump`、`Jump`、`Return`

如果后续要对 builtin 做更激进优化，可以再细分：

- 纯 builtin
- 读环境 builtin
- 写环境 builtin
- 可能抛错 builtin

## 3. Def-use / use-def 信息

SSA 的很多优化都依赖 use 链。

建议提供统一接口，至少支持：

- 查询某个定义被哪些指令使用
- 查询某条指令使用了哪些定义
- 在重写操作数后增量更新 use 链

这样后续做：

- 常量传播
- DCE
- copy propagation
- CSE

都会轻松很多。

## 与优化 pass 的关系

## SSA 之前适合先做的 pass

这些 pass 可以先在当前 IR 或“近 SSA IR”上落地：

- CFG 可达性清理
- 常量折叠
- 简单 copy propagation
- 删除未引用的纯表达式
- 分支条件常量化后的块裁剪

这样可以先建立 pass pipeline、验证框架和测试习惯。

## SSA 之后更值得做的 pass

这些 pass 在 SSA 上实现通常更自然：

- 全局常量传播
- SCCP
- 基于 use 链的 DCE
- 冗余 phi 删除
- 简单 CSE / GVN
- 死块删除后的 SSA 修复

## 推荐演进步骤

## Phase 0：先补优化框架

目标：

- 在 `lower_parsed_units_to_ir()` 之后引入 pass pipeline
- 支持 `optimize_module()` 或 `optimize_function()`
- 建立 pass 前后打印与验证机制

这一阶段先不要求 SSA。

## Phase 1：把当前 IR 推向 value-based

目标：

- 减少“按名字直接写回”在 IR 主体中的占比
- 让表达式和调用更多产出显式结果值
- 把返回值显式化

关键任务：

- 重构 `CallInstruction` 的输出建模
- 弱化 `AssignInstruction` 的中心地位
- 区分“值定义”和“名字绑定”

这是后续 SSA 化最关键的铺垫。

### 当前落地情况

这一阶段当前仓库已经完成了下面这些工作：

- `ValueId` / `ValueRef` / `InstValue` 已经进入 IR 核心类型
- `CallInstruction`、`ReturnInstruction` 已经支持 ref-first 构造
- `UnaryOpInstruction`、`BinOpInstruction`、`AssignInstruction`、`CondJumpInstruction` 也已经支持 ref-first 构造
- lowering helper 已经优先生成带 `ValueRef` 的 IR 节点
- `CallInstruction` 的输入已经不再保留兼容指针列表，只保留 `in_arg_refs`
- `CallInstruction` 的输出绑定已经不再保留在调用节点内部，而是由 lowering 显式追加 `AssignInstruction`
- `ReturnInstruction` 已经不再保留兼容的返回值指针列表
- `PhiInstruction` 已经进入 IR、打印器和解释器，并开始用于 `if` / `switch` merge block
- 解释器已经可以消费这些 `ValueRef`
- IR 打印已经可以显示 `values: %...` 和 `uses: %...`
- 解释器和打印器已经从“自己拼接旧字段和 ref 列表”收敛到统一的 ref-first 访问接口
- `UnaryOpInstruction`、`BinOpInstruction`、`AssignInstruction`、`CondJumpInstruction` 已经不再保留兼容操作数指针，打印会通过 `ValueId -> debug_name` 反查来保持可读性
- `ValueRef` 已经不再只是“已执行槽位引用”，而是开始具备“通过定义点回溯物化”的能力

这一阶段还没有完成的部分主要包括：

- 彻底移除这些节点上的旧 `Instruction*` 兼容字段
- 为 `NameInstruction` 找到更清晰的长期定位
- 建立统一的 def-use 分析结果对象
- 引入 verifier 和 side-effect model
- 把 phi 生成从当前的 `if` / `switch` merge 扩展到更系统的 SSA 插入流程

## Phase 2：建立分析层

目标：

- dominator
- dominance frontier
- def-use
- side effect model
- verifier

建议先写 verifier，检查：

- 每个 block 终结指令合法
- CFG 边一致
- 所有操作数定义存在
- phi 输入与前驱对应完整

## Phase 3：实现 mem2reg 风格的局部 SSA 化

如果仍保留一层“名字绑定/局部槽位”语义，可以先参考 mem2reg 思路：

- 找到可安全提升为 SSA 的局部名字
- 在 dominance frontier 插入 phi
- 通过 rename 把名字读写替换为 SSA 值

这个阶段可以只覆盖：

- 普通局部变量
- 形参
- 返回值名
- lowering 产生的隐藏变量

先不覆盖复杂别名场景。

## Phase 4：在 SSA IR 上落地第一批全局优化

优先级建议：

1. 常量折叠
2. 常量传播
3. DCE
4. CFG 简化
5. 冗余 phi 清理

这批 pass 的收益高、验证相对直接，也最能体现 SSA 的价值。

## Phase 5：决定 SSA 结果如何回到执行链

这里有两条路线。

### 路线 A：SSA 只作为优化中间层

流程：

- 当前 IR 转 SSA
- 运行优化
- 再 lower/回写成执行 IR
- 交给解释器执行

优点：

- 对现有解释器冲击小
- 容易分阶段落地

缺点：

- 需要维护一次 SSA 到执行 IR 的降级

### 路线 B：解释器逐步学会执行 value-based IR

流程：

- 当前 IR 演进到 value-based
- 解释器逐步从“名字表执行”转向“值结果执行”
- 再进一步接近直接执行优化后 IR

优点：

- 长期结构更统一

缺点：

- 短期改动更大

对当前项目，我更推荐先走路线 A。

## 执行链的三阶段演进

上面的路线 A 和路线 B 是“SSA 结果如何接回执行链”的两种短中期策略。

如果把视角放到更长期，当前项目更自然的主线其实是：

`名字环境解释器 -> value-based IR 解释器 -> SSA IR 解释器`

也就是说，SSA IR 解释器并不是不做，而是更适合作为后续统一执行架构的终点，而不是当前阶段的第一步。

### 阶段 1：名字环境解释器

当前状态基本属于这一阶段。

特点：

- `NameInstruction` 按名字读 frame
- `AssignInstruction` 按名字写 frame
- 调用结果仍会通过名字绑定进入 frame
- 返回值通过输出变量名收集

优点：

- 贴近源码语义
- lowering 简单
- 调试直观

缺点：

- 数据流隐式
- 不利于全局优化
- 不利于后续统一到 SSA

### 阶段 2：Value-based IR 解释器

这是最关键的过渡阶段。

目标：

- 让解释器逐步从“按名字执行”转向“按值结果执行”
- 但此时还不要求所有 merge 点都已经变成 `phi`

特点：

- 表达式和调用显式产出结果
- 结果可被后续指令直接引用
- 名字更多退化为 debug 或少量绑定用途
- 返回值变成显式操作数，而不是隐式去 frame 中收集

这一步完成后，解释器执行模型已经很接近“寄存器式 IR”，后续再引入 `phi` 会平滑很多。

### 阶段 3：SSA IR 解释器

当 value-based IR 稳定后，就可以进入完整 SSA 执行模型。

新增机制主要包括：

- `PhiInstruction` 或 block arguments
- 基于前驱块的 block entry 语义
- SSA rename 后的显式 def-use
- 多结果调用与返回值的 SSA 化

解释器执行时需要做到：

- 进入 block 时先根据前驱边解析所有 phi
- 再执行普通指令
- 再根据 terminator 跳转

### 为什么推荐这三阶段

因为直接从“名字环境解释器”跳到“SSA IR 解释器”会把下面这些事情绑成一次大改：

- lowering 重写
- IR 节点语义重写
- 调用返回模型重写
- 返回值模型重写
- block 入口语义新增
- 解释器状态模型重写

中间插入一个 value-based 阶段，可以显著降低一次性重构风险。

## 新的 IR 结果模型

为了让 IR 能平滑演进到 SSA，建议尽快引入“结果值是一等公民”的模型。

这里给出一个贴合当前项目的建议形状。

## 1. ValueId

每个“会产出运行时值”的定义都分配一个 `ValueId`。

例如：

- 字面量
- 一元运算
- 二元运算
- 调用结果
- `phi`
- 参数

可以把它理解成当前 IR 中“可被后续引用的值编号”。

建议约束：

- 一个 `ValueId` 只定义一次
- 一个指令可以定义 0 个、1 个或多个结果
- 终结指令通常不定义结果

## 2. ValueRef

所有“以某个值为输入”的地方，不再直接挂 `Instruction*` 或变量名，而是引用：

- `ValueId`
- 或少量特殊立即量句柄

也就是说，后续指令使用的是“前面某条定义产出的结果”，而不是“某个源码变量当前的名字”。

## 3. InstValue

建议把“指令定义的结果信息”收敛成统一结构。

例如逻辑上可写成：

- `value_id`
- `debug_name`
- `source_location`

其中：

- `value_id` 用于 def-use 和执行
- `debug_name` 用于打印成 `%x.3`
- `source_location` 用于调试和报错

这样既能满足 SSA，又不会丢失源码可读性。

## 4. 名字绑定与结果值分离

建议不要再让“名字本身”承担主要的数据流角色。

更合适的做法是引入一层轻量的名字绑定：

- `LocalBinding`
- `LocalSlot`
- 或 `NameBinding`

其职责只是表达：

- 当前源码名字 `x` 绑定到哪个 `ValueId`

在非 SSA / near-SSA 阶段，这层绑定可以显式存在；
在完整 SSA 阶段，这层绑定更多只用于 debug、打印和最终结果回收。

## 5. 指令结果分类

建议把 IR 指令按“是否定义结果”重新分类。

### 零结果指令

- `JumpInstruction`
- `CondJumpInstruction`
- 纯副作用调用

### 单结果指令

- `ConstInstruction`
- `UnaryOpInstruction`
- `BinOpInstruction`
- 大多数单返回值调用
- `PhiInstruction`

### 多结果指令

- 多返回值 `CallInstruction`
- 未来可能的解构或内建 helper

对当前项目，多结果是非常关键的一类，不能假设所有调用都是单结果。

## 6. CallInstruction 的新模型

当前：

- `name`
- `output_count`
- `in_arg_refs`

建议演进成：

- `callee`
- `in_args`
- `value_count`

并让调用本身定义若干结果。

逻辑文本形式例如：

- `%r0 = call sin(%x)`
- `%r0, %r1 = call size(%a)`

如果实现层不方便直接让一个节点拥有多个 `ValueId`，也可以先用：

- `CallInstruction`
- `ExtractResultInstruction`

的两层结构。

但从当前项目的语义出发，直接支持多结果调用会更自然。

## 7. ReturnInstruction 的新模型

当前返回值仍然更接近“函数结束时按输出名字去 frame 里找值”。

建议改成：

- `ReturnInstruction(values...)`

也就是返回指令显式携带返回结果。

例如：

- `return %r0`
- `return %r0, %r1`

这样有几个好处：

- 数据流更完整
- 更适合 SSA
- 更适合后续后端
- 不再依赖“函数尾部再去名字表里取值”

## 8. AssignInstruction 的收缩

`AssignInstruction(name, value)` 不适合作为长期主数据流节点。

建议把它逐步收缩成下面两类之一：

### 绑定型节点

表示：

- 源码名字 `x` 现在绑定到结果 `%r7`

这更像 debug / 符号层信息，而不是核心计算节点。

### 存储型节点

如果未来确实还需要一层非 SSA 的局部槽位模型，可以把它改成：

- `StoreLocal(slot, value)`
- `LoadLocal(slot)`

之后再通过 mem2reg 把它们提升成 SSA。

但如果目标是尽快进入 value-based IR，建议优先走“绑定型节点”，不要长期保留强语义的名字赋值节点。

## 9. NameInstruction 的去中心化

`NameInstruction(name)` 在当前 IR 里既承担：

- 变量读取
- 调试名字
- 调用输出目标

职责过重。

建议拆开：

- 读取值：改为显式 `ValueRef`
- 调试打印：使用 `debug_name`
- 输出绑定：由绑定层或多结果调用处理

这样后续 IR 会清爽很多。

## 10. 一个建议中的 near-SSA 形状

下面给出一个适合作为过渡阶段的逻辑例子。

源码：

```matlab
y = x + 1;
z = foo(y);
```

当前 IR 更像：

```text
x
1
(x + 1)
y = (x + 1)
y
[z] = foo(y)
```

建议中的 value-based / near-SSA 形状更像：

```text
%r0 = param x
%r1 = const 1
%r2 = add %r0, %r1
bind y <- %r2
%r3 = call foo(%r2)
bind z <- %r3
```

再往完整 SSA 继续推进时，`bind y <- %r2` 和 `bind z <- %r3` 的角色会继续减弱，最终数据流主要由 `%rN` 本身承载。

## 设计取舍建议

## 1. 不建议现在把整个项目一次性切到纯 SSA 执行

原因：

- 会同时冲击 lowering、IR 定义、打印、解释器和测试
- 很难局部验证问题来源
- 当前项目还处在基础设施建设阶段，一次性重构风险偏高

## 2. 建议先把“优化 IR”做 SSA，并让“执行 IR”按三阶段逐步靠拢 SSA

这是收益和风险比较平衡的路线。

这样既能：

- 尽快解锁全局优化
- 保留现有解释器作为稳定 fallback
- 同时不堵死后续演进到 SSA IR 解释器的路径

又不会把 JIT、SSA、解释器重写三件大事绑在一起。

## 3. 不建议长期停留在当前纯名字写回 IR

因为后续越往后做，下面这些问题会越来越明显：

- 跨块常量传播成本高
- DCE 很难做得既稳又不保守过头
- 调用多返回值的数据流不够干净
- 为 JIT 或更低层 IR 做 lowering 时边界模糊

## 风险点

## 1. MATLAB-like 动态语义会限制可 SSA 化范围

例如：

- 动态名字解析
- 环境可见性变化
- 复杂对象/索引写回
- 隐式工作区交互

所以第一阶段要明确：

- SSA 不是对所有语义一刀切
- 只对“可局部静态化”的那部分 IR 做 SSA

## 2. 多返回值与输出变量语义需要谨慎处理

函数调用和函数返回都不是单纯的单结果 SSA 语言模型，设计时必须从第一天起就考虑多结果定义。

## 3. debug 可读性不能丢

如果 SSA 名称完全失去源码名字，排查问题会很痛苦。

因此文档、打印和 verifier 都应把：

- 原始名字
- SSA 版本
- 源码位置

作为一等信息保留。

## 最终建议

对 Baltam_IR，推荐结论如下：

1. 现在就开始规划 SSA，但不要一次性把执行链全部重写成 SSA。
2. 先建立 pass pipeline 和分析基础设施，再把 IR 推向 value-based。
3. 在引入 JIT 之前完成“优化 IR 的 SSA 化”，不要等 JIT 再做。
4. 长期目标可以是 SSA IR 解释器，但中间最好先经过 value-based IR 解释器阶段。
5. 第一阶段只覆盖局部变量、参数、返回值和 lowering 生成的隐藏临时量。
6. 优先让 SSA 服务于常量折叠、常量传播、DCE 和 CFG 简化。
7. 现有解释器先保留为 fallback，优化结果通过降级、回写或双轨执行逐步接入执行链。

如果用一句话概括本方案：

`SSA IR 解释器可以作为长期目标，但当前最稳妥的路线是先把 IR 推向 value-based，再逐步收敛到完整 SSA。`

## 当前代码状态小结

如果只看当前仓库代码，而不是抽象方案，可以把状态总结成：

- 还没有完整 SSA
- 已经完成了 value-based IR 的第一轮基础设施铺设
- `Call` / `Return` / 条件 / 赋值 / 一元二元表达式都已经开始显式使用 `ValueRef`
- 解释器已经具备“名字环境 + value 槽位”的双轨执行能力
- 下一阶段最关键的工作是让更多节点真正变成 ref-first / value-first，而不是继续依赖旧的 `Instruction*` 兼容字段
