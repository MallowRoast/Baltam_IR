# mem2reg 设计

mem2reg 确定每条 load 的当前定义，将读取转换为 SSA 值引用，再移除不需要真实存储的 store。当前采用按需查询方案，仅依赖 CFG，不预先计算支配树、支配边界或变量活跃性。

实现位于 `src/Transform/mem2reg.h/.cpp`，入口为：

```cpp
PreservedAnalyses Mem2RegPass::run(FunctionUnit& function,
                                 FunctionAnalysisManager& analyses);
```

只处理 Non-SSA 函数，成功后发布 Based-SSA；脚本主体暂不处理。整体机制与 IR 字段背景见 [SSA 化与回退机制](SSA化与回退机制.md)。

## 1. 流程

```text
筛选 slot，计算入口可达块
→ 克隆 Non-SSA，建立参数与返回值接口
→ 从每条 load 按需查询当前定义
→ 必要时生成 PHI 或工作区读取
→ 将用户 store 转为 record，删除内部 store
→ 删除多余读取，简化 PHI
→ 更新恢复映射、压缩编号并发布 SSA
```

变换只操作副本。输入结构合法性由 Verifier 保证，Analysis 和 Transform 不重复验证。

## 2. 提升范围

SlotInfo 包含全局、持久声明或任一种共享关系时，保留相应 load/store。标记只表示声明存在，不代替运行时绑定。

不可达块中访问的槽位、MaybePack 写入目标，以及输入中已有带 SSA 来源 load 或 record 的槽位，也暂不提升。

内部循环计数器由 Builder 在 Non-SSA 阶段直接生成 PHI，结果及递增结果固定为 double 1x1，不再创建 `for.index.*` slot。mem2reg 保留已有 PHI。其他显式内部槽位仍参与按需定义查询和提升；忽略 workspace_barrier，不生成 record，也不进入用户工作区绑定表。

Non-SSA 已允许 PHI。短路表达式（包括直接赋值的 RHS）由 IRBuilder 直接生成两路 PHI，不创建临时 slot；mem2reg 克隆并保留这些寄存器汇合，仍只针对符合条件的 slot 消除 load/store，现有 PHI 同样参与后续简化。

参数使用函数签名直接定义的 Value，返回值由各条 ret 的输入给出。可提升参数在入口生成 record；需要保留存储的参数在入口生成 store。返回变量在每个返回点生成读取，与普通 load 一同求解。

for 的初始化和逐轮取值可能执行 size/double/subsref 重载，按 SSA 中常规调用不修改当前工作区的假设处理，不插入屏障，也不因此跳过提升。若实际修改父工作区，由运行时回退协议处理。显式 eval/load 工作区操作仍使用屏障。上限为 double 标量，下标也为 double，使用专用 GtDouble 比较，不使用动态 gt。

自动捕获分析尚未接入，嵌套函数及其父函数仍保守跳过。普通运算、重载及调用均按不修改当前工作区的 SSA 假设处理，不要求紧邻 workspace_barrier，也不因 Effect::MayWriteSlot 放弃槽位提升。显式工作区变化由 workspace_barrier 建模；Non-SSA 读写优化仍使用原有保守副作用规则。运行时工作区访问和异常保护协议仍待实现。

## 3. 值定义与变量绑定分离

store 没有结果，查询到它时直接使用输入 ValueId，不为赋值预留新寄存器。所有读取的定义图建立后，将可提升用户变量的 store 在原位置转换为：

```text
record slot_x, %value
```

RecordInst 只有 SlotId 和一个普通 Use，不产生值，也不做数据复制。运行时执行到这里才更新当前帧的槽位绑定；条件赋值的 record 保留在对应分支中。多个 slot 可分别引用同一个 SSA 值。内部 store 直接删除；全局、持久、共享等不可提升变量保留真实 load/store。

Value 不携带赋值目标。load 直接复用来源值，绑定更新由 record 独立保留。

## 4. 查询某个位置的当前定义

固定克隆指令的原始序列，辅助插入不改变查询位置。对于 load slot_x，从其前一条原指令开始逆序查找：

| 先遇到的相关指令 | 来源 |
| --- | --- |
| 同 slot 的 store | 直接使用 store 的输入 ValueId |
| 同 slot 的原 load | 该读取的结果，即使它尚未处理也可引用 |
| workspace_barrier | 用户槽位变为工作区来源；内部槽位忽略屏障继续查找 |
| 到达块首 | 查询当前块入口 |

无关指令直接跳过。相同变量的新赋值、读取或屏障自然截断对更早定义的查找。

如果当前 load 的来源是工作区，在其原位置前增加一个普通 load 取得状态，原读取暂时通过可选 SSA 来源连接定义图，随后直接传播来源并删除；后续读取可以复用已经取得的结果。辅助读取允许得到 Undefined，不在前驱路径上提前报错。

## 5. 块入口与出口查询

分别缓存：

```text
IN[B, slot]  → 块入口的定义
OUT[B, slot] → 块出口的定义
```

入口块使用参数初始值；没有入口定义的普通局部变量使用 Undefined。其他块先登记一个占位 PHI，再递归查询各个可达前驱出口，将结果作为输入。

出口查询等价于从原块末尾逆序执行第 4 节的规则。若先遇到 barrier，且后面没有同 slot 的赋值或读取，则在该块终结指令前补一次普通 load，并缓存结果，供多个后继复用。若已有读取则直接使用，不能跨后续屏障复用旧值。

```text
then:
    record slot_x, %a
    goto merge
else:
    call builtin.load(%file)
    workspace_barrier
    %x2 = load slot_x
    goto merge
merge:
    %x3 = phi [then, %a], [else, %x2]
    %r = load slot_x, %x3
```

辅助读取只取得绑定状态，不做名字消歧或重载，也不因 Undefined 提前报错，因此可放在前驱终结指令前。读取位置不能越过后续工作区修改。

不可达前驱不参与定义求解，只按 PHI 结构要求提供 Undefined 输入。

## 6. 循环与 PHI 清理

不能要求所有前驱先处理完成。查询入口时先将占位 PHI 写入缓存，再查询前驱；沿回边再次查询同一入口，直接返回已登记的 PHI，因此单前驱循环和多块循环都不会无限递归。

每个 (块, slot) 只创建一个入口 PHI。查询结束后统一清理：相同确定来源以及只含自身和一个确定来源的 PHI，可以替换为该来源。包含 Undefined 或没有外部来源的循环 PHI 保守保留。

实现可以先为单前驱入口创建占位 PHI，再在清理阶段消除。没有实际读取就不查询该变量，也不会为其构造 PHI 或工作区读取。

## 7. 读取、Undefined 与替换

```text
%state = load slot_x
%value = load slot_x, %state
%error = load slot_x, Undefined
```

无来源的 SSA load 取得槽位状态，允许 Undefined；有 SSA 来源的 load 不重新读取实际槽位，只是定义图构建期间的转发节点；对具体 ValueId 的转发最终消除。两者都不构成新的变量赋值。

定义图完成后，只要带来源 load 引用具体 SSA ValueId，就直接替换全部 Use 和恢复映射并删除，不再执行“必然已定义”分析。即使来源 PHI 可能携带 Undefined，也由 record、运算、调用、return 等普通指令实际取值时检查；PHI 只传递状态，不提前报错。显式 `load slot_x, Undefined` 没有可替代的 ValueId，暂保留原读取位置。真实工作区 load 仍保留。

## 8. 发布与恢复信息

候选用户 store 在原位置改为 record，内部 store 删除。简化 PHI、消除多余读取 后，再次清理 PHI。replace_all_uses 同时更新 record 的普通 Use 和跨阶段恢复来源，不为别名创建额外的结果编号。

record 的 Effect 为 MayWriteSlot，表示可观察的绑定提交；它不能因没有结果而被死代码消除，不能随意跨分支、工作区操作或可能失败的求值移动。原输入已有 record 的槽位保守跳过提升，避免把绑定记录当成普通存储重新转换。

最终按参数、返回值、函数体结果排列编号，更新跨阶段映射并发布 Based-SSA。克隆改变了块身份，旧 CFG 和其他已缓存分析均失效。

运行时来源表必须持有赋值时的结果，或使用可定位该次执行的句柄。静态 ValueId 在循环中会反复执行，仅记录其编号不能自动保证旧绑定的值仍可恢复；这部分留待执行器实现。

## 9. 验证范围

验证包含重复读取、分支汇合、Undefined、屏障后读取复用和补读取、多次工作区修改、条件读取不提前报错、循环零次和多次执行、回边屏障、参数与多返回值、普通运算和未附屏障的调用仍可提升，以及原基线、Use 链、支配关系和恢复映射的保留。

运行时工作区接口、Undefined 使用语义和实际回退尚未接入。此文描述 IR 构建与变换，不表示已有完整解释执行支持。

## 10. 函数接口与使用

SSA 参数是入口寄存器，普通参数和返回变量都参与提升：

```text
define @identity(%0 [name=x]) -> 1 {
; stage: Based-SSA

entry:
    record slot_x, %0
    ret %0
}
```

`[name=x]` 是源码变量关联，不是参数存储方式。参数 Value 由签名直接定义，不对应基本块指令。返回数量保存在阶段版本中，每个 ret 引用该路径的结果。需要实际存储的参数在入口建立 slot 绑定；返回变量在每个返回点取得当前定义。Non-SSA 签名和无操作数 ret 保持原有槽位约定。

```bash
cmake --build build -j4
./build/baltam_ir test/syntax/if_else.m -o /tmp/if_else.ir
./build/opt -passes=mem2reg /tmp/if_else.ir -o /tmp/if_else.ssa.ir
```

```cpp
FunctionPassManager passes;
passes.add_pass(Mem2RegPass{});
passes.run(module);
```

已经是 SSA 的函数再次运行时直接跳过。新版 IR 转换可独立测试，实际执行仍需接入工作区状态读取、定义检查和运行时回退机制。
