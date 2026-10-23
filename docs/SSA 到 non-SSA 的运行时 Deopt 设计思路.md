# SSA 到 non\-SSA 的运行时 Deopt 设计思路

本文记录 M 语言解释器及后续 JIT 中，遇到动态 workspace 操作时从 SSA 执行回退到 non\-SSA IR 的设计思路。当前内容是讨论结论与待办项，不代表所有运行时细节已经定案。



## **1\. 背景与目标**



当前设想的 IR 流程是先生成 non\-SSA IR，再生成 SSA IR。non\-SSA IR 通过静态 slot 表示具名变量，并用 `load` / `store` 指令显式访问 slot。SSA 执行时，变量的当前值由寄存器版本表示。



`eval`、`evalin`、`assignin` 等动态 workspace 操作可能按名字观察或修改变量绑定，因而可能破坏 SSA 对变量当前值的假设。初步方案是让常规执行尽量使用 SSA；当动态作用域操作实际需要时，将当前帧恢复为 non\-SSA，并由该帧继续执行。



`baltam::ba_obj` 已经是统一的动态类型包装，内部通过 `std::shared_ptr<type_base>` 持有数据。因此解释器中的 SSA 寄存器和 non\-SSA slot 若都保存 `ba_obj`，deopt 主要是恢复 slot 与 SSA 当前值之间的对应关系；通常不需要另做一次值装箱。后续 JIT 若使用 unboxed 表示，则需要另行定义物化规则。



## **2\. 基本模型**



```Plain Text
non-SSA IR:
  named slot + load/store

SSA IR:
  register values + phi

deopt state:
  visible static slot -> current SSA value

after deopt:
  materialize slot state and continue in non-SSA IR
```



deopt 恢复的是动态执行路径上每个可见静态 slot 当前对应的 SSA 值。若 SSA 寄存器在解释器中以稳定索引保存，可由寄存器编号引用；抽象层面应表达“当前 SSA 值位置”，以便未来 JIT 映射到机器寄存器、栈槽、常量或需物化的值。



运行时动态创建的新变量不加入静态 slot 表，也不进入 slot 到 SSA 值的 deopt 映射。



## **3\. 触发条件与执行边界**



### **3\.1 当前函数执行\`eval\`**



当前函数执行到 `eval` 调用时，从该调用开始切换到 non\-SSA。non\-SSA 执行器执行 `eval` 本身及后续代码。`eval` 的返回值和异常按普通函数调用处理。



这样 `eval` 的副作用只执行一次，不要求 SSA 执行器在调用之后继续解释动态代码对 workspace 的影响。



### **3\.2 Callee 修改 caller 的已有 slot**



callee 通过 `evalin('caller', ...)` 或 `assignin('caller', ...)` 成功修改 caller 中已有的静态 slot 时，标记该 caller 帧的 SSA 状态失效。callee 返回到 caller 的调用边界时检查标记；若已标记，则恢复 caller 的 slot 状态，并从 non\-SSA IR 中该调用之后的位置继续执行。调用本身不重放。



修改判定采取简单规则：只要对已有 slot 的写入成功，就算修改；不比较新旧值。若查找、校验或赋值未成功且 slot 未改变，则 caller 可以继续 SSA 执行。仅创建 caller 原先不存在的动态名字不触发 deopt。



`'base'` workspace 默认由 non\-SSA 执行。`'caller'` 指直接动态 caller；一次 `evalin` / `assignin` 操作只针对这个直接 caller。若更外层 workspace 被修改，需要由相应直接 callee/caller 边界逐层处理，不假设单次操作跨越多层栈帧。



### **3\.3 Deopt 后的执行模式**



一旦已有 slot 被成功修改，该帧的 SSA 假设即失效。deopt 后该帧保持 non\-SSA，不在同一次 activation 中重新进入旧 SSA 状态。



若 callee 成功修改 caller 的 slot 后又抛出异常，caller 仍先恢复为 non\-SSA，再按普通异常路径展开。异常处理逻辑必须观察到更新后的 slot 值。



## **4\. 回退状态的压缩**



为每个 deopt 点保存完整的 slot 快照会造成较大元数据。当前倾向采用静态增量映射或检查点：相邻节点复用已有映射，只记录相对前一状态变化的 slot 绑定；只有实际 deopt 时才重建完整的 slot → SSA 值映射并物化 slot。



deopt 预计较少发生，因此可接受恢复时沿增量描述重建状态。普通 SSA 热路径应尽量避免为了构造完整快照而额外复制所有 slot 值。



## **5\. 已明确的设计结论**



- non\-SSA 使用静态具名 slot，并通过 `load` / `store` 访问。

- SSA deopt 状态的核心是可见静态 slot 到当前 SSA 值的映射。

- 当前函数执行到 `eval` 时，从 `eval` 调用开始走 non\-SSA；返回值和异常遵循普通调用语义。

- callee 成功写入 caller 的已有 slot 后，在 caller 返回边界 deopt，并从调用之后继续，不重放调用。

- 成功写入已有 slot 即算修改，不比较值是否相等。

- 写入失败且 slot 未改变时，不需要 deopt。

- 只创建运行时动态变量不触发 deopt；动态变量不进入静态 slot 表。

- `'base'` 默认走 non\-SSA；`'caller'` 解析为直接 caller。

- 被修改的帧 deopt 后保持 non\-SSA。

- 成功修改后即使 callee 抛异常，caller 也先 deopt，再进入 non\-SSA 异常路径。

- 用静态增量映射/检查点压缩回退状态，在实际 deopt 时重建完整映射。

- 解释器使用 `ba_obj` 时，deopt 通常只需复制/共享包装对象，不需要重新装箱。

## **6\. 待设计与待调研**



### **6\.1 分支、phi 与循环中的映射（待进一步思考）**



SSA 中存在 phi 节点。分支合流后，slot 映射可指向 phi 结果；但增量映射如何表示实际控制流选择，循环头、回边及循环内反复更新如何限制映射链长度，尚未具体设计。这是当前最主要的未决项。



### **6\.2 M 脚本 workspace 语义（待调研 MATLAB 行为）**



需要调研脚本是否拥有独立帧/workspace、脚本如何共享执行者 workspace，以及脚本调用期间 `evalin('caller', ...)` 应解析到哪个 workspace owner。函数帧可由动态调用栈定位，脚本的 caller/workspace 规则需单独确认。



### **6\.3 Slot 特殊状态（计划由 slot 模型处理）**



以下状态尚未具体设计，计划纳入 slot 模型：



- 未初始化或当前未绑定的 slot；

- `clear` 后解除绑定及后续重新赋值；

- 闭包捕获变量；

- 共享 workspace / 共享绑定。

deopt 映射最终需要表达 slot 的绑定状态，而不能只表达普通的寄存器编号。



### **6\.4 调试与源码位置映射**



尚未考虑 SSA 位置到 non\-SSA 恢复位置的映射，以及栈回溯、源码位置和错误报告如何跨越 deopt 保持一致。



### **6\.5 JIT 值物化**



解释器 SSA 寄存器若持有 `ba_obj`，可直接共享/复制包装对象。JIT 若采用 unboxed 标量、机器寄存器分配复用或惰性对象物化，需要扩展 deopt state，将 slot 映射到可物化的位置并描述相应 boxing/materialization 操作。



## **7\. 实现时的正确性不变量**



deopt 后 non\-SSA 执行器应观察到与同一程序按 non\-SSA 语义执行至对应边界时一致的状态：



1. 每个可见静态 slot 的值及绑定状态正确；

2. 已发生的调用和写入副作用不重放、不丢失；

3. 恢复位置对应正确的调用前或调用后 continuation；

4. 异常路径保留成功写入的结果，并从正确的 non\-SSA 异常 continuation 继续；

5. deopt 后该帧不再使用失效的 SSA 值继续执行。

## **8\. 后续验证场景**



- 当前函数中的 `eval` 读取和修改已有静态变量；

- `evalin('caller', ...)` 读取 caller 变量但不修改；

- `assignin('caller', ...)` 成功修改已有 slot；

- `assignin` 失败或只创建动态新名字；

- callee 成功修改后正常返回，以及修改后抛异常；

- deopt 后的调用结果、异常和副作用只处理一次；

- 分支、phi、循环路径上的 slot 映射（待映射规则确定后）；

- 脚本作为 caller/callee 时的 workspace 解析（待 MATLAB 行为调研后）。

