# mem2reg 设计

## 1. 目标与范围

mem2reg 将能够安全提升的局部 slot 读写转换为直接的值引用。原本通过 `StoreSlotInst` 保存、再通过 `LoadSlotInst` 取出的值，在转换后直接通过 ValueId 使用；不同控制流路径产生的定义由 PHI 汇合。

实现位于 `src/Transform/mem2reg.h/.cpp`，入口为：

```cpp
PreservedAnalyses Mem2RegPass::run(FunctionUnit& function,
                                 FunctionAnalysisManager& analyses);
```

当前 pass 只处理处于 Non-SSA 阶段的函数，将结果发布为基础 SSA，即文本中的 `Based-SSA`。基础 SSA 可以保留不可提升 slot 的 load/store，并不要求所有工作区变量都消失。转换不涉及运行时对象的复制或数组数据搬运，也不进行常量折叠、公共子表达式消除或控制流简化。

脚本主体的 workspace 对外可观察，函数 pass 管理器不处理 `CommandUnit`。模块中的普通函数和局部函数可以参与转换；捕获与嵌套函数目前采用保守策略。

## 2. 输入前提与安全边界

Analysis 和 Transform 假定输入 IR 满足结构约束，包括合法的值定义、块归属、入口和终结指令。CFG 入口不能有前驱。完整正确性检查由后续独立的 Verifier Pass 承担，mem2reg 不重复执行结构验证。

可提升性分析判断的是优化能否保持语义。例如某个 slot 可能在赋值前读取，这是合法程序中可能触发的运行时行为，不能当作非法 IR，也不能直接删除这次读取。

### 2.1 函数级限制

出现以下任一情况，整个函数保持原阶段、原版本：

- 当前阶段不是 Non-SSA。
- 函数拥有嵌套函数。
- 函数属于模块中的嵌套函数，而非模块直接拥有的函数。
- 当前版本没有入口块。
- 存在除 slot 写入、全局声明和持久声明之外的 `Effect::MayWriteSlot` 指令。
- 没有任何可提升的 slot。

`Effect` 描述是否可能改写已有 slot，不等同于“是否有副作用”。动态调用、重载运算等操作可能影响工作区；当前缺少更精确的效果与别名信息，因此遇到这些操作时跳过整个函数，尚不做局部区间提升。扫描也包括不可达块，当前不会先删除不可达代码来放宽这一条件。

### 2.2 slot 级限制

| slot 的情况 | 当前处理 |
| --- | --- |
| 函数参数或返回值槽位 | 保留读写 |
| 被 GlobalDeclInst 或 PersistentDeclInst 声明 | 保留读写 |
| 在不可达块中发生读取或写入 | 保留该 slot 的全部读写 |
| 任意一次写入的值为 `Value::Form::MaybePack` | 保留读写 |
| 可达路径存在赋值前读取 | 保留读写 |
| 没有任何 load/store | 不参与提升 |
| 其余局部 slot | 参与提升 |

参数和返回值的存储约定、全局与持久绑定以及值包展开都需要额外语义支持。第一版优先保留这些行为。

## 3. 必然已定义分析

对每个候选 slot 单独进行前向数据流分析，判断每次读取之前是否已经完成写入。

对可达块 B：

```text
IN(entry) = false
IN(B) = 所有可达前驱 P 的 OUT(P) 的逻辑与
OUT(B) = IN(B) 或 B 中存在该 slot 的 store
```

非入口可达块的状态初始为 true，入口边界固定为 false，反复扫描直到出口状态稳定。前驱求交只考虑可达前驱。随后从每个块的 IN 状态出发，按指令顺序扫描：store 将状态置为 true；若 load 遇到 false，则取消该 slot 的提升资格。

这一分析同时处理跨块路径和块内顺序。例如一个分支给 x 赋值、另一个分支没有赋值，汇合处读取 x 时必须保留原 load，以保留未定义变量的运行时报错行为。

当前不通过生成 Undefined 值来代替原读取检查。PHI 的 Undefined 输入只是引用表示，不能自行提供运行时检查语义。

## 4. 在独立版本上转换

确定至少有一个可提升 slot 后，克隆源 `IRStageVersion`，在副本中完成后续步骤：

```text
筛选可提升 slot
    ↓
克隆 Non-SSA 版本
    ↓
按 slot 在迭代支配边界插入 PHI
    ↓
沿支配树重命名并填充 PHI 输入
    ↓
删除已替换的 load/store
    ↓
压缩值编号并发布 SSA 版本
```

源 CFG 和支配分析始终读取原 Non-SSA 版本。克隆保持基本块顺序，算法通过块索引定位副本中的对应块，不把源版本的块指针写入副本。

转换完成前不修改函数的当前阶段或发布目标版本。成功后通过 `set_stage_version(IRStage::SSA, version)` 发布，再选择 SSA 阶段。原 Non-SSA 版本继续保留；构建副本期间抛出异常时，不会发布部分转换结果。

## 5. PHI 放置：迭代支配边界

CFG、直接支配、支配树及支配边界由 [支配分析](支配分析设计.md) 提供。mem2reg 为每个可提升 slot 单独计算迭代支配边界 IDF：

1. 收集该 slot 的所有 store 所在块，去重后放入工作列表。
2. 弹出定义块 X，遍历 `frontier(X)`。
3. 若边界块 Y 尚未为该 slot 放置 PHI，则在块开头插入 PHI，并登记新结果。
4. PHI 也是新定义；若 Y 尚未加入工作列表，则将其加入。
5. 直到工作列表为空。

`phis[块索引][slot 索引]` 记录新 PHI，避免重复插入。PHI 连续位于基本块开头，每个结果是独立的 Value。

循环头可能位于自身的支配边界中，不能排除这一情况：循环头 PHI 正是用来汇合循环入口值和回边值的。

当前不结合活跃变量分析剪枝。因此，即使某个汇合后的值没有实际用途，也可能插入 PHI；也不在本 pass 中折叠所有输入相同的 PHI。这些清理可以由后续变换处理。

## 6. 沿支配树重命名

为每个 slot 维护一个当前定义栈，使用显式栈遍历支配树。进入与退出块分别执行：

| 时机 | 动作 |
| --- | --- |
| 进入块 | 保存所有 slot 定义栈的深度 |
| 处理块首 PHI | 将对应 slot 的 PHI 结果压栈 |
| 遇到可提升 slot 的 store | 将写入值压栈，记录待删除指令位置 |
| 遇到可提升 slot 的 load | 用当前栈顶替换 load 结果的所有使用，记录待删除位置 |
| 扫描完当前块 | 为每个 CFG 后继的 PHI 添加当前块对应的输入 |
| 退出块 | 将定义栈恢复到进入块时的深度 |

遍历支配树能让每个块继承支配它的定义；退出时恢复深度，避免一个兄弟分支的定义泄漏到另一个分支。

必然已定义分析保证参与提升的可达 load 有有效的栈顶。为后继 PHI 添加输入时，栈为空则显式记录 Undefined；这也可能发生在未剪枝但无后续读取的 PHI 上。

PHI 的前驱和输入通过 `add_incoming(predecessor, value)` 同时登记。可达区域重命名完成后，对不可达前驱补充 Undefined 输入，使每个真实前驱都有对应项。不可达块本身不参与重命名，来自同一前驱的重复控制流边只填一次输入。

## 7. Value、Use、User 与清理

load 结果的替换调用 `IRStageVersion::replace_all_uses`，沿 Value 的使用链修改 Use，不扫描整个函数，也不维护第二份公开操作数字段。后续读取 store 输入、动态操作数或 PHI 输入时，访问器会取得替换后的编号。

扫描阶段只记录待删除位置，不立即改变指令数组。重命名结束后，各块按位置倒序删除 load/store，避免删除导致后续位置偏移。删除 load 前，其结果使用已全部替换；删除 store 时，其输入 Use 自动脱链。

最后调用 `compact_values()`：移除空定义记录，更新存活 Value 的编号及指令结果编号。Value 地址保持稳定，已绑定 Use 自动从目标 Value 取得新编号，无须再遍历全部输入改写编号。SlotTable 保持原有槽位布局。

阶段克隆会创建独立 Value 和 Use，并重映射块引用；源、目标版本的使用链互不共享。具体所有权与销毁规则见 [IR 组织与缓存设计](IR组织与缓存设计.md)。

## 8. 示例

以下为分支场景的函数体片段，省略签名、slot 声明及常量元数据。slot0 为参数 c，slot1 为返回值 y，slot2 为局部变量 x。

转换前：

```text
bb0:
    %0 = load slot0
    branch %0, bb1, bb2
bb1:
    %1 = const !c0 (1): double 1x1
    store slot2 %1
    goto bb3
bb2:
    %2 = const !c1 (2): double 1x1
    store slot2 %2
    goto bb3
bb3:
    %3 = load slot2
    store slot1 %3
    ret
```

转换后：

```text
bb0:
    %0 = load slot0
    branch %0, bb1, bb2
bb1:
    %1 = const !c0 (1): double 1x1
    goto bb3
bb2:
    %2 = const !c1 (2): double 1x1
    goto bb3
bb3:
    %3 = phi [bb1, %1], [bb2, %2]
    store slot1 %3
    ret
```

x 的读写全部消失，y 直接使用 PHI 结果。参数和返回值仍沿用原有 slot 约定。具体 ValueId 和 PHI 输入顺序由当前块与遍历顺序决定。

### 循环中的定义汇合

对于入口初始化 x、循环体更新 x 的控制流，循环头需要同时接收入口值和上一次迭代的结果。提升后的核心结构如下，省略条件计算和退出块：

```text
entry:
    %0 = const !c0 (0): double 1x1
    goto header
header:
    %1 = phi [entry, %0], [body, %2]
    ...
body:
    %2 = intrinsic.int_add %1, %step
    goto header
```

其中 `%step` 表示在循环外已定义且支配循环体的步长值。此片段仅展示值关系，不是可直接读取的完整 IR 文件。

重命名先把循环头的 PHI 结果压入 x 的定义栈，循环体据此产生下一次定义。处理 body 到 header 的回边时，再为 PHI 补上 `%2`。PHI 可以引用文本中稍后出现的定义，读取器通过预留 Value 并补齐定义恢复这一关系。

## 9. Pass 管理与使用

`FunctionAnalysisManager` 按当前阶段版本按需构造 CFG 和支配分析。mem2reg 未转换时返回 `PreservedAnalyses::all()`；成功发布 SSA 后返回 `none()`。

提升本身没有改变控制流边，但副本中的基本块指针已经变化，因此源版本的分析不能作为新版本的分析继续使用。管理器清除旧结果，后续 pass 按需重建。分析失效不使用全局世界计数。

命令行：

```bash
./build/baltam_ir test/syntax/if_else.m -o /tmp/if_else.ir
./build/opt -passes=mem2reg /tmp/if_else.ir -o /tmp/if_else.ssa.ir
./build/opt -passes='function(mem2reg)' /tmp/if_else.ir -o /tmp/if_else.ssa.ir
```

C++ 管线：

```cpp
#include "Transform/mem2reg.h"

FunctionPassManager passes;
passes.add_pass(Mem2RegPass{});
passes.run(module);
```

重复运行时，已经处于 SSA 的函数直接跳过。若原函数因安全限制被跳过，则保持 Non-SSA，不会只修改阶段标签。

## 10. 后续方向

后续可结合 slot 活跃性减少 PHI，增加无用及平凡 PHI 清理，并在明确参数、返回值、捕获与值包语义后扩大提升范围。更精确的效果分析可以减少整函数跳过，但必须先证明相关操作不会破坏 slot 的定义关系。

验证应覆盖直线读写、分支汇合、循环回边、多层循环、定义前读取、不可达前驱和动态调用屏障，同时检查优化前后可观察结果、源 Non-SSA 的保留，以及 Value/Use 使用链的一致性。专门的 Verifier 仍是独立 pass。
