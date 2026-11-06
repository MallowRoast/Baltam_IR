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
→ 为候选 store 预留稳定赋值结果
→ 从每条 load 按需查询当前定义
→ 必要时生成 PHI 或工作区读取
→ 将 store 转为赋值 copy
→ 删除多余读取、简化 PHI、合并可省去的 copy
→ 更新恢复映射、压缩编号并发布 SSA
```

变换只操作副本。输入结构合法性由 Verifier 保证，Analysis 和 Transform 不重复验证。

## 2. 提升范围

SlotInfo 包含全局、持久声明或任一种共享关系时，保留相应 load/store。标记只表示声明存在，不代替运行时绑定。

内部迭代槽位、不可达块中访问的槽位、MaybePack 写入目标，以及输入中已有带 SSA 来源 load 或带 assigned_slot copy 的槽位，也暂不提升。

参数使用入口 ParameterInst 的寄存器定义，返回值由各条 ret 的输入给出。需要保留存储的参数在入口建立 slot 绑定。返回变量在每个返回点生成读取，与普通 load 一同求解。

自动捕获分析尚未接入，嵌套函数及其父函数仍保守跳过。MayWriteSlot 操作只有与后继 workspace_barrier 在同块紧邻时才按显式保护处理，否则整个函数保持 Non-SSA。运行时工作区访问和异常保护协议仍待实现。

## 3. 稳定赋值结果

为每条候选 store 预留 ValueId，记录：

```text
StoreSlotInst* → 最终赋值结果 ValueId
```

load 和 PHI 可以先引用它。完成读取分析后，在原 store 位置定义同编号的 copy，并设置 assigned_slot。原始 Non-SSA StoreSlotInst 仍不产生结果，不改变其文本格式。

如果常量或 intrinsic 紧邻 copy，且来源未关联其他 slot，可以转移 assigned_slot 并直接复用来源，省去 copy。其他赋值保留独立身份。

## 4. 查询某个位置的当前定义

固定克隆指令的原始序列，辅助插入不改变查询位置。对于 load slot_x，从其前一条原指令开始逆序查找：

| 先遇到的相关指令 | 来源 |
| --- | --- |
| 同 slot 的 store | 预留的赋值结果 |
| 同 slot 的原 load | 该读取的结果，即使它尚未处理也可引用 |
| workspace_barrier | 工作区来源，不能继续越过屏障查旧定义 |
| 到达块首 | 查询当前块入口 |

无关指令直接跳过。相同变量的新赋值、读取或屏障自然截断对更早定义的查找。

如果当前 load 的来源是工作区，在其原位置前增加一个普通 load 取得状态，原读取通过可选 SSA 来源保留使用语义；后续读取可以复用已经取得的结果。辅助读取允许得到 Undefined，不在前驱路径上提前报错。

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
    %x1 = copy %a to slot_x
    goto merge
else:
    call builtin.load(%file)
    workspace_barrier
    %x2 = load slot_x
    goto merge
merge:
    %x3 = phi [then, %x1], [else, %x2]
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

无来源的 SSA load 取得槽位状态，允许 Undefined；有 SSA 来源的 load 不重新读取实际槽位，在原变量读取位置保留读取和未定义错误语义。两者都不构成新的变量赋值。

定义图完成后，通过输入传播判断值是否必然已定义，移除可证明多余的带来源 load。这个分析只服务于读取简化，不用于预先放置 PHI，也不替代未来 clear 绑定有效性检查。

## 8. 发布与恢复信息

将候选 store 改为预留结果编号的 copy，再统一删除旧 store。普通值替换通过 replace_all_uses 更新 Use 和恢复来源；不能因没有普通 Use 删除仍承担变量赋值的定义。

简化 PHI、消除多余读取、合并安全的赋值 copy 后，再次清理 PHI。最终按参数、返回值、函数体结果排列编号，更新跨阶段映射并发布 Based-SSA。克隆改变了块身份，旧 CFG 和其他已缓存分析均失效。

## 9. 验证范围

验证包含重复读取、分支汇合、Undefined、屏障后读取复用和补读取、多次工作区修改、条件读取不提前报错、循环零次和多次执行、回边屏障、参数与多返回值、未保护调用保持原阶段，以及原基线、Use 链、支配关系和恢复映射的保留。

运行时工作区接口、Undefined 使用语义和实际回退尚未接入。此文描述 IR 构建与变换，不表示已有完整解释执行支持。

## 10. 函数接口与使用

SSA 参数是入口寄存器，普通参数和返回变量都参与提升：

```text
define @identity(%0: x) -> 1 {
; stage: Based-SSA

entry:
    ret %0
}
```

`: x` 是源码变量关联，不是参数存储方式。ParameterInst 由签名隐式定义，不在块内重复打印。返回数量保存在阶段版本中，每个 ret 引用该路径的结果。需要实际存储的参数在入口建立 slot 绑定；返回变量在每个返回点取得当前定义。Non-SSA 签名和无操作数 ret 保持原有槽位约定。

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
