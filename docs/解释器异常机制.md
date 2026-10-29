# 解释器与 JIT 的异常机制设计说明

> 状态：设计说明与实现草案，非现有实现规范。
> 范围：语言异常语义、SSA IR、递归式解释器、LLVM JIT、二者互操作。
> 核心选择：语言 IR 描述异常区域；解释器和 JIT 通过统一的 C++ 异常载体互传异常；真实调用栈由 C++ 异常处理机制展开。

本文讨论 MATLAB 的 `try/catch` 和 `MException`。`VMThrown`、`ExceptionHandle` 与下文的异常 IR 均为**拟议接口**。

## 1. 异常机制概述

先看一个 MATLAB 例子：

```matlab
function A()
    try
        B();
    catch ME
        disp(ME.message);
    end
end

function B()
    guard = onCleanup(@() disp('离开 B'));
    error('Demo:Failure', '读取失败');
end
```

`A` 调用 `B` 时处于 `try` 区域内。`B` 中的 `error` 产生并抛出异常；`B` 没有处理器，异常便沿调用链传到 `A`。离开 `B` 时，`guard` 的清理动作执行；随后进入 `A` 的 `catch`，异常绑定到 `ME`，供处理代码读取。异常的标识符、消息和抛出位置可用于诊断。如果 `A` 也没有有效的 `catch`，异常会继续向外传播，最终由外层执行边界处理。

处理器是否有效取决于调用位置：即使 `B` 再调用 `C` 并由 `C` 抛出异常，只要 `A` 调用 `B` 的位置仍在 `try` 内，`A` 的 `catch` 就有效。若有多个有效的 MATLAB `catch`，选择动态调用链上最近的一个；MATLAB 不按异常类型、标识符或消息筛选 `catch`。

从这个例子可以抽象出异常机制的组成部分。无论采用哪种运行时实现，都要回答以下问题：

| 部分 | 需要解决的问题 |
| --- | --- |
| 异常值 | 抛出的是什么？它可以是对象或其他形式的值；类型、原因、消息等信息由语言定义。 |
| 抛出操作 | 在哪个执行点中断正常控制流，并开始异常传播？ |
| 异常区域与处理器 | 哪段代码受保护？抛出时哪些处理器（handler）有效？若语言有筛选规则，如何判断处理器是否适用？ |
| 异常传播 | 当前执行位置无法处理时，如何沿动态调用链继续查找处理器？ |
| 栈展开与清理 | 异常向外传播时，如何退出沿途执行帧，并按规定顺序释放资源、运行清理操作？ |
| 捕获状态 | 进入处理器后，如何取得异常值，支持嵌套捕获或重抛，并在离开时恢复先前状态？ |
| 未捕获异常 | 整条调用链都没有适用的处理器时，异常由哪个外层边界接收，程序如何报告或终止？ |
| 诊断信息 | 如何记录抛出位置和调用路径等信息，供报错和调试使用？ |

一次异常的典型控制流是：

```text
抛出异常值
  → 沿动态调用链查找适用的活动处理器
  → 展开到该处理器，并执行沿途清理
  → 进入处理器；若始终没有适用的处理器，则到达未捕获异常边界
```

上述流程只表示概念上的顺序；具体实现可以先查找再展开，也可以在查找过程中逐帧展开。栈展开会执行规定的清理，但不会自动撤销写入文件、修改对象等已发生的副作用。

### 1.1 MATLAB 异常对象与抛出语义

`catch ME` 取得的是用户可见的 `MException` 对象，而不是原生异常载体。对象至少要提供 `identifier`、`message` 和结构化的 `stack`；`cause` 等属性属于语言对象接口。新建对象的栈可以为空，实际抛出时才记录位置和调用链。[MathWorks：MException](https://www.mathworks.com/help/matlab/ref/mexception.html)、[try/catch](https://www.mathworks.com/help/matlab/ref/try.html)。

MATLAB 的 `catch` 对进入相应 `try` 的语言异常统一生效，`ME.identifier` 等属性可供 catch 体内检查，但不参与选择处理器。本文后面提到的 `VMThrown` 类型识别仅用于区分 MATLAB 异常的原生传播载体与解释器内部的其他 C++ 异常。

| 操作 | 对语言栈信息的要求 |
| --- | --- |
| `throw(ME)` | 从当前抛出位置生成栈信息。 |
| `throwAsCaller(ME)` | 将异常定位为由调用者抛出。 |
| `rethrow(ME)` | 保留已捕获异常的原始栈信息。 |

这三种操作规定了不同的栈语义，仍通过同一套传播与捕获机制执行。[MathWorks：throw](https://www.mathworks.com/help/matlab/ref/mexception.throw.html)、[throwAsCaller](https://www.mathworks.com/help/matlab/ref/mexception.throwascaller.html)、[rethrow](https://www.mathworks.com/help/matlab/ref/mexception.rethrow.html)。`ME.stack` 供程序读取，`getReport` 和最外层错误显示负责生成文本报告；捕获异常本身不应触发未捕获错误的显示或记录。[MathWorks：getReport](https://www.mathworks.com/help/matlab/ref/mexception.getreport.html)、[MException.last](https://www.mathworks.com/help/matlab/ref/mexception.last.html)。

### 1.2 设计背景

在本项目的 MATLAB 语言实现中，M 函数可能由解释器执行，也可能由 JIT 编译后执行。执行方式不应改变 MATLAB 的异常语义：相同的抛出应找到相同的 `catch`，沿途执行应有的清理，并向用户提供一致的异常对象和诊断信息。因此，解释器需要知道如何维护活动处理器并退出执行帧；JIT 需要把同一套异常区域和处理规则转换成机器码可执行的异常控制流。

异常的处理器还可能位于另一种执行方式的调用者中。例如 `解释器 A（try） → JIT B → 解释器 C（throw）`，异常要穿过 JIT 帧回到 `A`；反过来，JIT 函数也可能捕获解释器函数抛出的异常。只规定各自内部如何抛出和捕获还不够：两侧必须约定同一种可识别的异常、跨边界传播方式，以及栈展开时的清理、对象生命周期和栈信息记录。这也是本文同时讨论解释器、JIT 及其互相调用的原因。

## 2. Julia 与 LLVM 如何表示异常

### 2.1 Julia 的语言 IR

Julia 的 lowered IR 有 `enter`、`leave`、`pop_exception`、`the_exception` 等形式。其官方 AST 文档描述：`enter` 设置 catch 目标并产生 token；`leave` 退出 handler；`pop_exception` 恢复与 `enter` 对应的已捕获异常状态；`the_exception` 取得当前捕获的异常。[Julia ASTs：Lowered form](https://docs.julialang.org/en/v1/devdocs/ast/#Lowered-form)。

它们可以概念性地读作：`enter catch_label` 开始保护范围；正常路径上的 `leave` 撤销保护；异常路径进入 `catch_label` 后，通过 `the_exception` 取得异常；catch 完成时 `pop_exception` 恢复此前的捕获上下文。这里描述的是控制流含义，具体 IR 形状仍应以 Julia 对应版本的文档为准。

要注意**表示形式随层次和版本不同**：该 AST 文档将 `leave` 的参数描述为要弹出的 handler 数量；[JuliaLowering 的说明](https://github.com/JuliaLang/JuliaLowering.jl#lowering-of-exception-handlers) 展示了以 token 恢复 handler 状态的形式。本文自己的 IR 选用 token，但不把它冒充 Julia 所有版本的精确语法。

Julia SSA IR 还使用 `Upsilon` 与 `PhiC` 表示异常路径上的值。一个 catch 可能从受保护区域内多个抛出位置进入；如果变量在区域内多次赋值，catch 应看到**实际抛出时**的值。`Upsilon/PhiC` 让这个关系保留在较高层 IR 中，而不必过早把每个可能抛出的调用拆成 LLVM 风格的显式异常边。[Julia SSA-form IR：PhiC nodes and Upsilon nodes](https://docs.julialang.org/en/v1/devdocs/ssair/#PhiC-nodes-and-Upsilon-nodes)。

**借鉴的是异常语义和 SSA 表达，不是 Julia 当前的底层展开方案。** Julia 文档将其 `enter` 与 `setjmp` 联系起来；Julia 项目的 [issue #56466](https://github.com/JuliaLang/julia/issues/56466) 讨论过从 `setjmp/longjmp` 转向更接近 C++ 的异常机制。本文选择 C++ 异常处理机制（C++ Exception Handling，简称 **C++ EH**）作为解释器/JIT 的跨边界载体。

### 2.2 LLVM IR 的异常控制流

LLVM IR 更接近目标平台的原生 EH。常见的 Itanium 风格 ABI 在处理 C++ 异常时先搜索匹配的处理器，再展开原生栈并执行清理；其他平台的具体规则不同。[Itanium C++ ABI：The Unwind Process](https://itanium-cxx-abi.github.io/cxx-abi/abi-eh.html)。对于适用 Itanium 风格 EH 的目标：

- `invoke` 表达一个可能抛出的调用，同时指定正常目标和异常目标。
- `landingpad` 是异常目标的入口；它取得异常记录和类型选择结果。`resume` 继续传播未处理的异常。
- 函数的 `personality` 决定如何解释异常信息；catch 类型、清理与原生 ABI 配合。
- Windows/MSVC 风格 EH 使用 `catchswitch`、`catchpad`、`cleanuppad` 等 funclet 结构，不能原样套用 Itanium 的 `landingpad` 形状。

这些是 LLVM 的异常 IR；Julia SSA 文档示例中形如 `invoke Main.foo()` 的 Julia 调用打印形式，不等同于 LLVM 的 `invoke` 终结指令。[LLVM Language Reference：invoke](https://llvm.org/docs/LangRef.html#invoke-instruction)、[LLVM Exception Handling](https://llvm.org/docs/ExceptionHandling.html)。

| 问题 | Julia 风格语言 IR | LLVM EH IR |
| --- | --- | --- |
| 哪个区域受保护 | `enter/leave` 等语言状态 | 每个可能抛出调用的异常边及原生异常元数据 |
| 捕获时的值 | `the_exception`、`PhiC/Upsilon` | EH 入口的异常值，另加普通 SSA、`phi` 或显式保存的值 |
| 如何传播 | 由语言运行时或所选后端决定 | 目标平台 EH ABI、`resume` 或 funclet 的继续展开路径 |
| 是否独立于平台 | 较高层，适合解释器和多种后端 | EH 结构受目标平台和 personality 约束 |

## 3. 解释器阶段的拟议设计

### 3.1 语言异常 IR

以下指令是**本项目候选接口**，名称和精确 token 类型仍可调整：

| 指令 | 作用 |
| --- | --- |
| `enter_handler target -> token` | 激活当前 Frame 内的 handler，并记录恢复所需状态。 |
| `leave_handler token` | 正常离开受保护区域，使 handler 失效。 |
| `throw value` | 将语言异常对象交给统一抛出入口。 |
| `current_exception` | 读取当前 catch 正在处理的语言异常。 |
| `pop_exception token` | 恢复进入对应保护区域前的已捕获异常状态。 |
| `rethrow value` | 按语言规则重新抛出指定异常；是否支持无参数形式需另定。 |

语言 IR 还须保留 `throw`、`throwAsCaller`、`rethrow` 的栈记录策略；可以由抛出指令携带模式，也可以使用不同指令，具体编码待定。这不需要三套异常传播通道。

IR 必须约束每条正常、异常、`return`、`break`、`continue` 路径上的状态恢复。`leave_handler` 仅处理 handler 的有效性；`pop_exception` 仅处理已捕获异常上下文。异常跳入 catch 前，要自动停用所选 handler 及跨越的内层 handler，避免 catch 再次抛错时回到自身。例如外层 catch 正处理异常 A，内层 catch 又捕获 B；内层 catch 结束后应恢复 A，而不是清空已捕获异常上下文。

### 3.2 异常相关运行时结构与解释执行

每次语言函数调用都进入一层真实 C++ 调用，并持有自己的语言 Frame。下面仅展示异常机制涉及的主要结构与字段：

```cpp
struct ExceptionHandler {
    HandlerId id;
    BlockId catchTarget;
    ScopeDepth scopeDepth;
};

struct Frame {
    InstructionId pc;
    std::vector<ExceptionHandler> activeHandlers;
};

struct CaughtException {
    ExceptionHandle exception;
    CatchToken token;
};

struct ExecutionContext {
    Frame* currentFrame;
    std::vector<CaughtException> caughtStack;
};

struct VMThrown {
    ExceptionHandle exception;
};
```

`Frame.activeHandlers` 记录本帧当前有效的处理器，`pc` 用于异常命中后跳到 catch 入口；`scopeDepth` 用于确定跨越了哪些语言级清理作用域。`ExecutionContext.caughtStack` 保存正在处理的异常，供 `current_exception`、嵌套 catch 和重抛使用。其他普通执行状态不在此列出。

`ExceptionHandle` 表示语言异常对象的稳定、可持有引用，可以根据对象所有权模型选择智能指针。`VMThrown` 只用于原生传播，不充当用户可见的 `MException` API。已捕获异常和展开中的载体必须保持对象存活。`ExecutionContext` 应按线程或任务隔离，而非使用一个无隔离的进程全局槽位。

统一抛出逻辑将语言异常的 handle 放入原生传播载体。面向 JIT 的入口使用 C ABI，仅传递不透明指针；下例假设入口会取得自己的持有引用，指针的具体所有权协议仍需在实现时固定：

```cpp
struct VMExceptionOpaque;

[[noreturn]] void throwVM(ExceptionHandle exception) {
    throw VMThrown{std::move(exception)};
}

extern "C" [[noreturn]] void vm_throw(VMExceptionOpaque* exception) {
    throwVM(retainException(exception));
}
```

`extern "C"` 只指定 `vm_throw` 的符号链接方式；入口内部仍由 C++ EH 抛异常。JIT 无需了解 `ExceptionHandle` 的 C++ 对象布局，也不能把 C ABI 误解为异常按 C 机制传播。JIT 调用该入口时仍要满足目标平台调用约定与异常 ABI。

解释器的通用执行入口可以写成：

```cpp
Value Interpreter::execute(Frame& frame) {
    FrameScope frameScope(context, frame); // RAII 维护调用链

    for (;;) {
        try {
            return executeUntilExit(frame); // 从 frame.pc 执行
        } catch (const VMThrown& thrown) {
            auto handler = frame.findInnermostActiveHandler();
            if (!handler) {
                throw; // 同一原生异常继续向调用者传播
            }

            frame.leaveScopesCrossedByException(*handler);
            frame.deactivateThrough(*handler);
            auto catchToken = context.pushCaughtException(thrown.exception);
            frame.associateCatchToken(*handler, catchToken);
            frame.pc = handler->catchTarget;
        }
    }
}
```

这是状态转换示意，**不是可直接合入的代码**：清理失败、非 `VMThrown` 原生异常的边界、提前离开 catch、栈追踪时机和 token 的所有权都需要最终规范。`catch (const VMThrown&)` 是对传播载体的识别；MATLAB 的 `catch` 不做异常类型筛选。C++ EH 只析构真实 C++ 栈上的对象；语言虚拟作用域的清理必须由解释器显式运行。

### 3.3 完整例子：A 调用 B，B 抛出

```matlab
function A()
    try
        B();
    catch ME
        disp(ME.message);
    end
end
```

概念性语言 IR：

```text
A.entry:
    %h = enter_handler A.catch
    call B()
    leave_handler %h
    jump A.exit

A.catch:
    %ex = current_exception
    store ME, %ex
    call disp(ME.message)
    pop_exception %h
    jump A.exit
```

执行过程：

1. A 执行 `enter_handler`，A Frame 的 `activeHandlers` 含 `A.catch`。
2. A 调用 B；B 的某个 `throw` 创建或取得 `ExceptionHandle`，记录语言抛出位置，执行 `throw VMThrown{handle}`。
3. B 没有 handler 时 `throw;`；B 的 C++ 帧被展开，A 的解释器捕获点得到 `VMThrown`。
4. A 查到 `A.catch`，退出相应语言作用域，使这个 handler 失效，并把异常放入 `caughtStack`。
5. A 从 `A.catch` 继续解释；`current_exception` 提供 `ME`。正常离开 catch 时，`pop_exception` 恢复此前状态。

若 A 的 catch 内再次抛错，新的异常交给**外层**有效 handler；A 的这个 catch 不捕获自身。若 A 也没有有效 handler，则异常继续到调用者，直至最外层 VM API 边界。该边界将未处理的 `VMThrown` 转换为失败结果和错误显示，而不替代函数内的语言 catch。

## 4. JIT 阶段的拟议设计

### 4.1 编译时如何确定异常边

解释器在运行时查看当前 Frame 的 `activeHandlers`；JIT 则在编译时利用语言 IR 的 `enter_handler/leave_handler`，确定**每个可能抛出异常的调用点**受哪个处理器保护。机器码通常不再动态压入和弹出语言 handler。调用点与异常入口的对应关系由 LLVM IR 和最终生成的异常表表达。

| 语言 IR | JIT 中的处理 |
| --- | --- |
| `enter_handler` / `leave_handler` | 划定编译时的保护范围，确定调用点应连接的异常入口。 |
| 保护范围内的 `call B()` | 生成 `invoke @B`：正常返回进入正常块，异常展开进入本函数的 EH 入口。即使实际抛出发生在 B 的更深层调用中，A 的这个调用点仍能接住异常。 |
| `throw value` | 建立语言异常对象并调用 `vm_throw`，由它抛出 `VMThrown`。如果抛出点位于本函数的保护范围或清理范围内，这次调用也要有相应的异常边。 |
| `current_exception` / `pop_exception` | 在 catch 路径取得语言异常对象，并在离开 catch 时恢复需要保留的捕获状态。 |

没有本地 catch、也没有异常清理的函数，可以让异常穿过其调用帧继续向上传播；它不必为每个调用都生成本地 `landingpad`。有本地清理时，即使不捕获异常，也必须生成能进入清理代码并继续传播的异常路径。任何可能抛出的调用都不能被错误标为 `nounwind`。[LLVM Language Reference：invoke](https://llvm.org/docs/LangRef.html#invoke-instruction)、[LLVM Exception Handling：Cleanups](https://llvm.org/docs/ExceptionHandling.html#cleanups)。

### 4.2 `A` 调用 `B` 的 LLVM IR 示意

沿用第 3.3 节：A 在 `try` 内调用 B，B 抛出异常，A 的 `catch` 处理它。假设 B 没有本地 catch 或清理，下面展示 **Itanium 风格目标**上的主要控制流；`ptr`、类型信息和 catch 体均为简化表示，**这不是可直接执行的完整 LLVM IR**：

```llvm
; B 没有本地处理器或清理，异常从 B 继续向调用者传播。
define void @B() {
entry:
  %h = call ptr @make_exception()
  call void @vm_throw(ptr %h)
  unreachable
}

define void @A() personality ptr @__gxx_personality_v0 {
entry:
  invoke void @B()
      to label %normal unwind label %eh

normal:
  ret void

eh:
  %lp = landingpad { ptr, i32 }
      catch ptr @VMThrown_typeinfo
  %record = extractvalue { ptr, i32 } %lp, 0
  %selector = extractvalue { ptr, i32 } %lp, 1
  %wanted = call i32 @llvm.eh.typeid.for(ptr @VMThrown_typeinfo)
  %matches = icmp eq i32 %selector, %wanted
  br i1 %matches, label %catch_vm, label %propagate

catch_vm:
  ; 按目标 C++ ABI 开始原生 catch，并从 VMThrown 取得 ExceptionHandle。
  ; 执行 MATLAB catch；离开时结束原生 catch。
  ret void

propagate:
  resume { ptr, i32 } %lp
}
```

关键区别在调用点：B 的 `call @vm_throw` 可以直接向外传播，因为 B 没有本地处理工作；A 的 `invoke @B` 必须给出异常目标 `%eh`，因为 A 的 `try` 要捕获来自 B 的异常。若 B 自己有 catch 或语言级清理，B 的相应调用点也要改成带异常边的形式。

`landingpad` 给出的是原生异常记录和类型选择值，**不是** `MException`。`personality` 与异常表配合识别唯一的语言异常载体 `VMThrown`；确认后还需按目标 C++ ABI 开始原生 catch，取得并保留 `ExceptionHandle`，才能执行 MATLAB 的 catch。这里的类型判断只区分 `VMThrown` 和其他 C++ 异常，**不根据 `MException` 的类型或标识符选择 MATLAB 处理器**。其他 C++ 异常继续传播；对于仅含 `VMThrown` catch 子句且没有清理动作的入口，personality 通常会直接跳过这个入口。示意代码中的 `%propagate` 展示继续展开的路径，并省略了 catch 体再次抛错时的异常边，以及所有出口都要结束原生 catch 的清理路径。[LLVM Exception Handling：Try/Catch](https://llvm.org/docs/ExceptionHandling.html#try-catch)。

### 4.3 机器码运行时如何传播异常

上例运行时按以下顺序发生：

1. B 创建语言异常对象；`vm_throw` 抛出持有 `ExceptionHandle` 的 `VMThrown`。
2. 平台异常处理机制沿真实调用栈查找处理器。B 没有本地处理器，异常继续向 A 传播；A 的调用位置由 `invoke` 关联到 `%eh`。
3. 展开经过的帧并执行它们规定的清理，然后进入 A 的 `%eh`。JIT 机器码无需逐帧运行 `findInnermostActiveHandler()`；处理位置已编码在异常表中。
4. A 的 EH 入口确认异常载体是 `VMThrown`，开始原生 catch，取得 `ExceptionHandle`，执行 MATLAB catch。`current_exception` 从保存的 handle 取得语言异常对象。离开 catch 时结束原生 catch，并恢复语言级捕获状态。
5. 若 A 没有活动的 MATLAB 处理器，异常继续传播到更外层的解释器或 JIT 调用者，最终可能到达 VM API 边界。

LLVM 负责生成机器码及异常元数据，平台的 C++ EH 负责查找和展开**原生调用帧**；VM 仍需定义语言异常对象、语言级清理和栈信息。JIT 的链接与装载流程还须确保生成代码的异常帧信息已向进程的 unwinder 注册，否则异常无法可靠地跨越 JIT 帧。[LLVM Exception Handling：Overview](https://llvm.org/docs/ExceptionHandling.html)、[LLVM JITLink 文档](https://llvm.org/docs/JITLink.html)。

### 4.4 其他语言语义与运行时职责

- **语言级清理：** 原生展开不会自动执行 JIT 函数中的 `onCleanup` 等语言动作；lowering 必须在异常路径生成相应清理，并在需要继续传播时使用 `resume`。catch 体再次抛错也必须经过必要的清理路径。
- **异常路径上的值：** 若 `try` 内有多个可能抛出的调用，catch 中的局部变量应取实际抛出时的值。lowering 需要把语言 IR 的 `PhiC/Upsilon` 等信息变成合法的 LLVM SSA 合流或显式保存的值。
- **异常对象生命周期与捕获状态：** 结束原生 catch 时，`VMThrown` 载体可能被销毁；若 MATLAB catch 还要使用异常对象，须先持有独立的 `ExceptionHandle`。嵌套 catch 或无参数重抛若依赖当前异常上下文，也要维护相应的语言级捕获状态。
- **语言栈信息：** JIT 要保留从机器码位置到 MATLAB 源位置的映射，并按抛出方式生成或保留结构化的 `ME.stack`。内联后，一个原生帧可能对应多个语言调用帧，不能直接把原生回溯当作 `ME.stack`；文本报告再由 `getReport` 或外层错误显示生成。

以上是 Itanium 风格的示意；Windows/MSVC 风格目标须使用 `catchswitch`、`catchpad`、`cleanuppad` 等 funclet 结构，不能照搬 `landingpad` 控制流。[LLVM Exception Handling：Windows and MSVC](https://llvm.org/docs/ExceptionHandling.html#windows-and-msvc-exception-handling)。

## 5. 解释器与 JIT 之间的异常传递

前提：双方位于**同一进程、同一执行线程的可展开调用链**，使用兼容的 C++ EH ABI，以及同一套可识别的 `VMThrown` 类型信息。MATLAB 层总是选择最近的活动 `catch`；底层的 `VMThrown` 类型信息只用于避免把其他 C++ 异常交给这个 catch。

| 抛出端 → 捕获端 | 传播路径 |
| --- | --- |
| 解释器 → 解释器 | `VMThrown` 到调用者的 C++ 捕获点；调用者 Frame 查活动 handler。 |
| JIT → 解释器 | JIT 帧按已注册的异常元数据展开；解释器捕获点查询 Frame。 |
| 解释器 → JIT | 解释器无活动 handler 时 `throw;`；JIT 调用位置的 EH 入口识别 `VMThrown`。 |
| JIT → JIT | 平台 EH 沿 JIT 帧查找原生异常入口。 |

例如 `JIT A → 解释器 B → JIT C`：C 抛出后，若 B 有活动的语言 handler，B 就处理并正常返回 A；否则 B 继续抛出，A 的 JIT 异常入口可能捕获。反方向 `解释器 A → JIT B` 同理。**边界处只需按各自的模型处理或放行同一个 `VMThrown`，不能无意中将它转换成普通返回值。**

还要满足以下约束：

- JIT 与运行时的调用入口可采用 C ABI，只传递不透明指针等 ABI 明确的参数；不要让 JIT 按值传递 `ExceptionHandle`、`std::shared_ptr` 等 C++ 对象。`VMThrown` 的类型定义、RTTI、C++ 标准库与异常 ABI 在解释器、JIT、动态库之间一致；`extern "C"` 不会把跨帧传播改成 C 式错误返回。
- 桥接函数不得错误声明为 `noexcept`/`nounwind`；不允许原生异常穿越的外部接口须在受控边界转换为 `VMThrown` 或转换为显式错误结果。
- `ExceptionHandle` 在展开和 catch 期间保持对象存活；JIT 的源位置映射要与解释器语言 Frame 配合生成连续的语言栈信息。
- 若执行跨线程、跨进程或远程 ORC executor，不直接传播 C++ 异常对象；需要序列化语言异常并在目标执行上下文重新建立传播。

受控原生函数边界可以按下列次序转换异常。它是策略示意：哪些原生异常应转换、哪些应作为进程级错误，仍须逐类规定。

```cpp
Value callNative(...) {
    try {
        return nativeFunction(...);
    } catch (const VMThrown&) {
        throw; // 保持原有语言异常
    } catch (const std::exception& e) {
        throwVM(makeMException("NativeError", e.what()));
    }
}
```

最外层 VM 执行入口另设兜底捕获点，将未处理的 `VMThrown` 转成 API 失败结果、更新 `MException.last` 并生成错误显示；它不承担语言函数内部 `try/catch` 的选择职责。若保留 `lasterr`，其兼容更新规则还须单独定义，但它不用于跨帧传播异常。

## 6. 附录：其他异常实现机制

| 机制 | 查找与传播方式 | 主要取舍 |
| --- | --- | --- |
| 显式错误返回 | 每层函数返回状态或 `Result`，调用者检查并转发。 | 控制流显式；需要每层配合，原生栈不会因错误自动展开。 |
| 解释器虚拟栈展开 | VM 自己维护调用帧和 handler，抛错时弹出虚拟帧。 | 不依赖 C++ EH；必须自行保证清理、原生互操作和 JIT 边界。 |
| `setjmp/longjmp` | 进入保护区时保存恢复点，抛错时跳到该点。 | 需要动态建立恢复点；裸 `longjmp` 不会替语言运行虚拟作用域清理，也不能直接当成 C++ RAII 展开的替代品。 |
| 表驱动原生 EH | 编译器生成异常表与 unwind 信息，抛错时由原生 unwinder 查找和展开。 | 正常路径一般无逐次保存上下文的 `setjmp` 成本；异常路径、元数据和 ABI 接入较复杂。 |

LLVM 文档区分了基于 `setjmp/longjmp` 的 SJLJ 模型与表驱动模型，并说明前者需要运行时注册/撤销上下文，后者把异常信息放在带外表中；“零成本”指正常路径的特定成本模型，**不表示抛出异常免费**。[LLVM Exception Handling：SJLJ 与 zero-cost](https://llvm.org/docs/ExceptionHandling.html)。Julia 的 [issue #56466](https://github.com/JuliaLang/julia/issues/56466) 讨论了 `setjmp/longjmp` 对优化的影响，是选择底层机制时的参考，不是本项目 IR 必须照搬的规则。

### 6.1 `setjmp/longjmp` 的工作方式

最简化的形式是在进入 `try` 时调用 `setjmp` 保存恢复点，抛出时保存异常对象并 `longjmp` 回该恢复点：

```c
jmp_buf point;
if (setjmp(point) == 0) {
    run_try_body();
} else {
    run_catch_body();
}
```

这个片段省略了异常对象保存、嵌套 handler 管理、清理和生命周期处理。它说明的是“跳回先前保存的位置”，不是 C++ EH 的处理器搜索和逐帧析构。原始 `longjmp` 也不会替 VM 执行语言级清理；若跨越有 C++ 析构责任的帧，更不能将它直接当作 C++ 异常展开来用。解释器若采用此路线，须独立设计清理协议和原生边界。[Open Group：setjmp](https://pubs.opengroup.org/onlinepubs/009696899/functions/setjmp.html)、[LLVM Exception Handling：SJLJ](https://llvm.org/docs/ExceptionHandling.html)。

## 7. 实现前须固定的语义与验证点

1. 固定 `enter/leave/pop_exception` token 的作用域、异常边上的状态转换，以及正常/提前/异常退出 catch 时的恢复规则。
2. 固定 `throw(ME)`、`throwAsCaller(ME)`、`rethrow(ME)`、可选无参数 `rethrow` 的栈追踪和异常对象身份语义。
3. 固定 `onCleanup` 等语言级清理的顺序，以及清理再次抛错时的替换、链式原因或终止规则。
4. 固定 `ExceptionHandle` 的所有权、任务隔离，以及最外层失败结果、`MException.last` 和兼容 `lasterr` 的更新规则。
5. 用四种解释器/JIT 调用组合验证跨帧捕获；再覆盖嵌套 catch、catch 内再抛、跨帧清理和优化编译。
6. 分目标平台验证 JIT 异常元数据注册、`VMThrown` 载体识别、C ABI 调用入口、外部函数边界及未处理异常的行为。

这些约束完成后，语言 IR 可以作为解释器和 JIT 的共同语义来源；两端各自实现控制流，但对用户呈现同一套 `try/catch`、重抛和异常对象行为。
