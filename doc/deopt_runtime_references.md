# Deopt 与优化运行时参考资料

本文整理 deopt、safepoint、stack map、guard、失效机制和优化运行时相关资料。它不是设计方案本身，而是给
[execution_strategy.md](./execution_strategy.md) 中的 tiered execution / deopt 设计提供参考入口。

## 1. 建议阅读顺序

如果目标是先服务当前 M 语言解释器 / JIT 设计，建议按这个顺序读：

1. Self dynamic deoptimization
2. JavaScriptCore speculation / OSR exit
3. LLVM StackMaps / Statepoints
4. V8 lazy unlinking / speculative deopt
5. Deoptimization correctness paper
6. Graal empirical deoptimization / Truffle optimizing docs
7. RPython guard handling / tracing JIT resume data

原因是：

- Self 资料先建立 deopt 的基本模型：优化代码如何回到未优化执行。
- JavaScriptCore 资料把 speculation、profile tier、OSR exit 和 watchpoint 解释得比较工程化。
- LLVM 资料解释工程层面如何记录恢复点、live value 和 GC root。
- V8 资料更接近现代动态语言运行时中的 guard、dependency 和 code invalidation。
- 正确性论文适合后续设计 deopt invariant 和测试准则时参考。
- GraalVM 和 RPython 资料适合理解不同 JIT 架构下 deopt / guard failure 的实现方式。

## 2. Deopt 基础

### Dynamic Deoptimization in the Self System

链接：

- https://bibliography.selflanguage.org/dynamic-deoptimization.html

关注点：

- optimized frame 如何恢复成 interpreter frame。
- deopt 不应回滚已经提交的副作用。
- 优化代码需要在 deopt point 保存足够的逻辑状态。
- 高 tier 代码失败后应回到唯一语义基线。

对当前设计的启发：

- `Tier 0` 应是唯一完整语义基线。
- `Tier 1` / `Tier 2` 不能通过重新执行整段函数来修复状态。
- deopt map 应描述如何从当前优化状态继续执行，而不是从函数入口重跑。

## 3. Stack Map / Statepoint

### LLVM StackMaps and Patchpoints

链接：

- https://llvm.org/docs/StackMaps.html

关注点：

- 在机器码中特定位置记录 live value 的位置。
- 支持 runtime patching、inline cache 和 deopt state recovery。
- stack map 是 JIT 和 runtime 协作的底层元数据。

对当前设计的启发：

- `Tier 1` 即使不做复杂优化，也应保留 IR continuation map / safepoint 入口。
- `Tier 2` 的 guard failure 需要 deopt state map。
- 后续 LLVM JIT 需要把 M runtime 的逻辑 frame 映射到机器码 live state。

### LLVM Statepoints

链接：

- https://llvm.org/docs/Statepoints.html

关注点：

- 在 GC / relocation / safepoint 场景下显式描述 live reference。
- helper call、allocation 和 deopt point 周围需要明确状态边界。

对当前设计的启发：

- 如果后续 runtime 有移动 GC 或复杂对象 relocation，普通 call metadata 不够。
- `eval`、`clear`、`assignin`、动态 dispatch helper 这类强语义边界应自然成为 safepoint 候选。
- deopt 与 GC root 枚举最好共用一套状态描述基础设施。

## 4. Guard、依赖与失效

### V8 Lazy Unlinking of Deoptimized Functions

链接：

- https://v8.dev/blog/lazy-unlinking

关注点：

- 优化代码失效后如何从调用入口和依赖关系中脱钩。
- 不一定要立即扫描和修改所有调用点，可以懒惰地处理失效代码。

对当前设计的启发：

- `path_epoch`、`workspace_epoch`、`builtin_epoch` 变化后，可以先标记 code object invalid。
- 后续进入或调用该代码时再回退到 Tier 0 / Tier 1。
- 失效机制不必第一版就做全局即时 patch。

### V8 Wasm Speculative Optimizations and Deopts

链接：

- https://v8.dev/blog/wasm-speculative-optimizations

关注点：

- profile 反馈驱动的投机优化。
- guard 失败后回退到较低 tier。
- 优化假设、依赖关系和 deopt point 的协作。

对当前设计的启发：

- `Tier 1` 可以收集 profile，`Tier 2` 基于 profile 生成 typed SSA。
- `Tier 2` 不应把动态边界吞进 SSA，而应围绕稳定 region 做 guard。
- guard failure 应回到对应 IR continuation，而不是重新解释整个函数。

## 5. GraalVM / Truffle 运行时资料

### GraalVM Safepoint

链接：

- https://www.graalvm.org/21.3/graalvm-as-a-platform/language-implementation-framework/Safepoint/index.html

关注点：

- 语言实现中如何在长时间运行的代码里插入 safepoint。
- safepoint 与中断、取消、线程协作之间的关系。

对当前设计的启发：

- 数值循环和长 region 需要 safepoint / interrupt check。
- OSR entry / OSR exit、deopt、debugger 和异步中断可以共享 safepoint 机制。

### GraalVM Optimizing

链接：

- https://www.graalvm.org/jdk25/graalvm-as-a-platform/language-implementation-framework/Optimizing/

关注点：

- profile、partial evaluation、specialization 与 optimization 的关系。
- 动态语言运行时如何用 guard 保护优化假设。

对当前设计的启发：

- `Tier 2` 可以把 typed SSA 看成从稳定 profile region 中抽取出来的优化表示。
- 强动态操作应保留为 runtime helper / safepoint，而不是强行内联进数值优化 region。

## 6. Correctness / Invariant

### Correctness of Dynamic Deoptimization

链接：

- https://arxiv.org/abs/1711.03050

关注点：

- deopt 正确性的形式化条件。
- 优化执行状态和未优化执行状态之间如何建立对应关系。
- deopt point、state reconstruction 和 continuation 的正确性。

对当前设计的启发：

- 每个 deopt point 都应有明确的 continuation PC。
- state map 需要覆盖用户可观察变量、临时值、返回值和异常状态。
- deopt 测试不能只测结果值，还要覆盖 workspace、global、persistent、path 和 side effect 顺序。

## 7. Deopt 论文与技术文档清单

这一节把 deopt 相关资料按用途整理。前面章节已经出现过的资料也会在这里重复列出，方便按问题查阅。

### 7.1 基础模型

#### Debugging Optimized Code with Dynamic Deoptimization

链接：

- https://bibliography.selflanguage.org/dynamic-deoptimization.html

关键词：

- dynamic deoptimization
- optimized frame reconstruction
- source-level debugging
- interrupt point

建议重点：

- optimized frame 如何恢复成 source-level frame。
- deopt point 为什么需要保存足够恢复信息。
- 已提交副作用不应通过“重跑函数”来修正。

#### HotSpot Uncommon Trap / Deoptimization

链接：

- https://openjdk.org/groups/hotspot/docs/HotSpotGlossary.html

关键词：

- uncommon trap
- speculative optimization
- interpreter frame
- recompilation

建议重点：

- JVM 如何把低概率路径编译成 trap。
- guard 失败后如何回到解释器。
- 哪些 profiling 信息会驱动重新编译。

### 7.2 Speculation / Guard / OSR Exit

#### Speculation in JavaScriptCore

链接：

- https://webkit.org/blog/10308/speculation-in-javascriptcore/

关键词：

- speculation
- profiling tier
- watchpoint
- OSR exit
- exit flag

建议重点：

- profile 如何形成 speculation。
- speculation 失败后如何 OSR exit。
- watchpoint 和 dependency invalidation 如何配合。

#### JavaScriptCore Overview

链接：

- https://docs.webkit.org/Deep%20Dive/JSC/JavaScriptCore.html

关键词：

- LLInt
- Baseline JIT
- DFG
- FTL
- OSR

建议重点：

- 多 tier 如何组织。
- baseline、DFG、FTL 之间如何传递 profile。
- OSR entry / exit 在 tiered execution 中的位置。

#### V8 Lazy Unlinking of Deoptimized Functions

链接：

- https://v8.dev/blog/lazy-unlinking

关键词：

- code invalidation
- dependency tracking
- lazy unlinking
- optimized code lifecycle

建议重点：

- 代码失效不一定要立即 patch 所有调用点。
- 依赖失效可以先标记 optimized code，再在入口处懒处理。
- 这对 `path_epoch` / `builtin_epoch` / `workspace_epoch` 很有参考意义。

#### V8 Wasm Speculative Optimizations and Deopts

链接：

- https://v8.dev/blog/wasm-speculative-optimizations

关键词：

- speculative optimization
- deopt
- feedback
- guard failure

建议重点：

- profile feedback 如何驱动优化。
- guard failure 如何回到较低 tier。
- deopt point 如何和优化假设绑定。

### 7.3 State Map / Stack Map / Safepoint

#### LLVM StackMaps and Patchpoints

链接：

- https://llvm.org/docs/StackMaps.html

关键词：

- stack map
- patchpoint
- live value location
- runtime patching

建议重点：

- 机器码位置如何映射到 runtime 可读的 live state。
- JIT 如何记录寄存器、栈槽、常量等恢复信息。
- 后续 `DeoptStateMap` 可以借鉴它的结构。

#### LLVM Statepoints

链接：

- https://llvm.org/docs/Statepoints.html

关键词：

- safepoint
- GC root
- relocation
- live reference

建议重点：

- helper call / allocation / deopt point 附近如何枚举 live reference。
- deopt state 和 GC root map 为什么最好共用底层元数据。

#### GraalVM Safepoint

链接：

- https://www.graalvm.org/21.3/graalvm-as-a-platform/language-implementation-framework/Safepoint/index.html

关键词：

- safepoint
- guest language interrupt
- long-running code

建议重点：

- 长循环如何响应中断和 runtime 请求。
- OSR、deopt、debugger、cancel 可以共享 safepoint 机制。

### 7.4 Graal / Truffle

#### An Empirical Study on Deoptimization in the Graal Compiler

链接：

- https://drops.dagstuhl.de/entities/document/10.4230/LIPIcs.ECOOP.2017.30

关键词：

- Graal
- deoptimization cost
- warmup
- recompilation
- empirical study

建议重点：

- deopt 对 warmup 和稳定性能的影响。
- 哪些 deopt 原因常见。
- deopt 后的重新编译策略。

#### GraalVM Optimizing Truffle Interpreters

链接：

- https://www.graalvm.org/jdk25/graalvm-as-a-platform/language-implementation-framework/Optimizing/

关键词：

- Truffle
- self-optimizing interpreter
- partial evaluation
- transfer to interpreter

建议重点：

- AST interpreter 节点如何收集 profile 并特化。
- 何时 transfer to interpreter。
- 动态语言 runtime 如何把解释器和优化编译器连接起来。

### 7.5 Trace JIT / Side Exit

#### The Efficient Handling of Guards in the Design of RPython's Tracing JIT

链接：

- https://stups.hhu-hosting.de/downloads/pdf/schneider_efficient_2012.pdf

关键词：

- guard
- tracing JIT
- side exit
- bridge
- resume data

建议重点：

- guard failure 后如何构造 resume data。
- trace JIT 如何为 hot side exit 生成 bridge。
- 这和 region JIT 的 deopt state map 有相似结构。

#### PyPy JIT Documentation

链接：

- https://doc.pypy.org/en/latest/jit/index.html

关键词：

- meta-tracing
- guard
- bridge
- resume data

建议重点：

- 从解释器描述生成 trace JIT 的方式。
- guard failure 后如何恢复解释执行。
- 适合作为“解释器优先”路线的参考。

#### LuaJIT Internals

链接：

- https://wiki.openresty.org/LuaJIT/

关键词：

- trace compiler
- side exit
- snapshot
- machine code trace

建议重点：

- trace snapshot 如何服务 side exit。
- 热路径和 side exit 如何组织。
- 对比 region JIT 和 trace JIT 的状态恢复差异。

### 7.6 Correctness / Verification

#### Correctness of Speculative Optimizations with Dynamic Deoptimization

链接：

- https://arxiv.org/abs/1711.03050

关键词：

- speculation
- assumption
- deoptimization
- IR semantics
- correctness

建议重点：

- 如何把 assumption 和 deopt 放进 IR 语义。
- 优化状态和未优化状态之间如何建立对应关系。
- 对当前 M IR 的 `guard + deopt_state + continuation_pc` 设计很直接。

#### Formally Verified Speculation and Deoptimization in a JIT Compiler

链接：

- https://popl21.sigplan.org/details/POPL-2021-research-papers/46/Formally-Verified-Speculation-and-Deoptimization-in-a-JIT-Compiler

关键词：

- verified JIT
- speculation
- synchronization point
- deoptimization

建议重点：

- deopt synchronization point 的形式化定义。
- guard、assumption、continuation 之间的正确性约束。
- 适合后续设计 verifier 和 deopt 测试时参考。

### 7.7 对当前项目最关键的三个概念

当前阶段读这些资料时，先聚焦三个工程概念：

```text
DeoptPoint:
  优化代码可以在这里安全回退。

DeoptStateMap:
  描述如何从寄存器、栈槽、常量、SSA value、materialized object 恢复解释器可见状态。

IRContinuation:
  回退后从哪个 CodeUnit / BasicBlock / Instruction 继续执行。
```

这三个概念对应到当前 M runtime：

- `DeoptPoint` 应出现在 guard failure、helper call、dynamic dispatch、eval/clear/assignin 前后。
- `DeoptStateMap` 应覆盖 frame slot、workspace binding、global/persistent 状态、临时值和返回位次。
- `IRContinuation` 应指向 Tier 0 的 IR 位置，避免从函数入口重新执行导致副作用重复。

## 8. 与当前 M Runtime 设计的映射

这些资料对应到当前设计，可以形成以下工程约束：

```text
Tier 0:
  完整语义基线，负责 workspace/eval/clear/dispatch 的真实行为。

Tier 1:
  可使用更快的 slot / cache / IC，但动态危险操作需要通过 runtime helper。
  helper 在修改 workspace/global/path 前负责触发 deopt 或 materialization。

Tier 2:
  在 profile 稳定 region 上构造 typed SSA。
  所有投机假设都由 guard + deopt state map 保护。

Runtime:
  维护 workspace/global/path/builtin epoch。
  维护 safepoint、IR continuation map、deopt state map 和 code invalidation。
```

当前最重要的设计原则：

- deopt 不回滚已经提交的全局副作用。
- 强动态 helper 应在执行副作用前 materialize / deopt 受影响 frame。
- 优化代码应回退到 IR continuation，而不是从函数入口重新执行。
- epoch / invalidation 只能发现假设失效，不能代替 deopt state recovery。

## 9. 活跃课题组 / 产业实验室

这一节整理和动态语言 JIT、tiered execution、deopt、IC、runtime specialization 相关的活跃工程团队和研究方向。它不是完整名单，主要按当前 M runtime 设计的相关性筛选。

### 9.1 生产级动态语言 VM / JIT 团队

#### Oracle Labs / GraalVM / Truffle

链接：

- https://www.graalvm.org/
- https://www.graalvm.org/dev/community/publications/

关注点：

- self-optimizing AST interpreter
- partial evaluation
- dynamic language specialization
- guard / deopt / safepoint
- polyglot runtime

对当前设计的价值：

- 很适合作为“完整语义解释器 -> optimized runtime”的长期参考；其 AST/bytecode 选择不作为本项目方案。
- 对 `eval`、动态 dispatch、specialization 和 safepoint 的工程分层有参考价值。
- Truffle 的思想适合研究“解释器节点如何携带 profile 并逐步特化”。

#### Google V8

链接：

- https://v8.dev/
- https://v8.dev/blog/maglev

关注点：

- Ignition bytecode interpreter
- Sparkplug baseline compiler
- Maglev mid-tier compiler
- TurboFan optimizing compiler
- inline cache、hidden class、deopt、dependency invalidation

对当前设计的价值：

- 适合参考多 tier 分层、IC、guard 和 deopt 的生产级组合。
- Maglev 对 `Tier 1.5 / mid-tier` 的设计很有参考意义。
- V8 的失效和 deopt 资料可以帮助设计 path/workspace/builtin epoch。

#### WebKit JavaScriptCore

链接：

- https://docs.webkit.org/Deep%20Dive/JSC/JavaScriptCore.html

关注点：

- LLInt
- Baseline JIT
- DFG JIT
- FTL JIT
- OSR entry / OSR exit

对当前设计的价值：

- 多 tier 结构清晰，适合理解解释器、baseline、优化 JIT 之间如何传递 profile 和状态。
- DFG/FTL 的分层对后续 typed SSA / LLVM tier 有参考价值。

#### Mozilla SpiderMonkey

链接：

- https://firefox-source-docs.mozilla.org/js/
- https://spidermonkey.dev/

关注点：

- JavaScript bytecode
- Baseline interpreter / JIT
- WarpMonkey
- IC、guard、deopt、GC integration

对当前设计的价值：

- 适合对照 V8/JSC，观察另一个生产 JS VM 如何组织 tiering 和 IC。
- 对 runtime helper、GC root、debugger 和 deopt 的耦合边界有参考意义。

#### Shopify / Ruby YJIT

链接：

- https://docs.ruby-lang.org/en/master/yjit/yjit_md.html
- https://shopify.engineering/ruby-yjit-is-production-ready

关注点：

- CRuby 内置 JIT
- lazy basic block versioning
- side exit
- 低成本、生产导向的 JIT 工程

对当前设计的价值：

- 非常适合参考“在已有解释器语义上加 JIT”的工程路径。
- 比 V8/Graal 更轻量，适合作为早期 `Tier 1` 或简单 `Tier 2` 的现实参照。

#### Meta HHVM

链接：

- https://hhvm.com/
- https://engineering.fb.com/2016/09/22/networking-traffic/redesigning-the-hhvm-jit-compiler-for-better-performance/

关注点：

- Hack/PHP VM
- profile-guided optimization
- region compilation
- server workload JIT

对当前设计的价值：

- 对 `profile -> hot region -> optimized code` 的路线很相关。
- 适合参考 region-based optimizing JIT 的工程组织。

#### CPython Faster CPython / Experimental JIT

链接：

- https://docs.python.org/3.14/whatsnew/3.14.html#binary-releases-for-the-experimental-just-in-time-compiler

关注点：

- specializing adaptive interpreter
- bytecode / uop interpreter
- experimental JIT
- 在强动态语言上渐进引入优化 tier

对当前设计的价值：

- CPython 的路线更接近“先稳解释器，再逐步加 profile 和 JIT”。
- 适合作为保守 tiering 策略参考。

#### Julia Compiler / JuliaHub / Julia 社区

链接：

- https://docs.julialang.org/en/v1.13-dev/devdocs/jit/

关注点：

- LLVM JIT
- multiple dispatch
- type inference
- world age
- 数值计算语言优化

对当前设计的价值：

- Julia 和 Matlab/M 的数值计算场景更接近。
- multiple dispatch、类型推断、函数版本化和 world age 对动态函数分派设计很有价值。

### 9.2 学术课题组 / 研究方向

#### JKU Linz SSW

链接：

- https://ssw.jku.at/Research/Projects/JVM/

关注点：

- Graal / Truffle
- dynamic compilation
- language implementation framework

对当前设计的价值：

- 适合跟踪 Truffle/Graal 背后的论文和实验系统。
- 对自优化解释器、partial evaluation、deopt 有长期参考价值。

#### Tokyo Tech Programming Research Group

链接：

- https://prg.is.titech.ac.jp/projects/

关注点：

- runtime compilation
- RPython / meta-tracing
- dynamic language VM

对当前设计的价值：

- 适合参考 meta-tracing JIT 和动态语言 VM 生成方向。
- 对“从解释器定义自动得到 JIT”的路线有参考意义。

#### Stanford AHA / Deegen

链接：

- https://aha.stanford.edu/deegen-meta-compiler-approach-high-performance-vms-low-engineering-cost

关注点：

- JIT-capable VM generator
- interpreter + baseline JIT generation
- dynamic language VM meta-compiler

对当前设计的价值：

- 很适合关注低工程成本生成解释器和 baseline JIT 的思路。
- 对当前项目早期构建 `Tier 0/Tier 1` 框架有启发。

#### IIT Bombay PLATO

链接：

- https://www.cse.iitb.ac.in/plato/

关注点：

- dynamic language optimization
- JavaScript / R 等语言运行时优化
- program analysis

对当前设计的价值：

- R 和 Matlab 在数据分析/数值计算场景上有相似性。
- 可关注其动态语言优化和程序分析方向。

#### University of Kent Programming Languages and Systems / Stefan Marr

链接：

- https://research.kent.ac.uk/programming-languages-systems/
- https://www.cs.kent.ac.uk/people/staff/sm951/pubs.html

关注点：

- dynamic language VM
- SOM 系列实验语言运行时
- Truffle / RPython / interpreter tooling

对当前设计的价值：

- 适合跟踪动态语言实现、benchmark、runtime tooling 和 interpreter/JIT 对比研究。

### 9.3 JIT / Runtime 基础设施

#### LLVM ORC / JITLink

链接：

- https://llvm.org/docs/ORCv2.html
- https://llvm.org/docs/JITLink.html

关注点：

- LLVM JIT linking
- lazy compilation
- symbol resolution
- runtime code management

对当前设计的价值：

- 如果 `Tier 2` 最终走 LLVM，这是需要重点理解的基础设施。

#### Bytecode Alliance / Cranelift

链接：

- https://cranelift.dev/

关注点：

- fast code generation
- Wasm runtime
- lower-latency compiler backend

对当前设计的价值：

- 适合作为 baseline JIT 或中间 tier 后端的参考。
- 编译速度比 LLVM 更适合 warmup 敏感场景。

#### Eclipse OpenJ9 / OMR / JitBuilder

链接：

- https://eclipse.dev/openj9/docs/jit/
- https://github.com/eclipse/omr

关注点：

- JVM JIT
- reusable runtime / compiler components
- JITBuilder

对当前设计的价值：

- 适合参考成熟 VM 如何组织 JIT、runtime helper、GC、stack map 和 code cache。

### 9.4 对当前项目的跟踪优先级

当前 M runtime 更建议优先跟踪：

```text
Graal / Truffle:
  动态语言 specialization、safepoint、deopt、runtime framework。

V8 / JSC / SpiderMonkey:
  多 tier、IC、guard、dependency invalidation、生产级 deopt。

YJIT / HHVM:
  在已有解释器语义上逐步引入 JIT；region-based / block-versioning 路线。

Julia:
  数值计算语言、multiple dispatch、type inference、LLVM JIT。

CPython JIT:
  保守动态语言解释器如何渐进进入 tiered execution。
```

从当前设计阶段看，最直接有用的是：

- 其他系统的解释器语义基线：CPython、YJIT、SpiderMonkey。
- `Tier 1` / baseline JIT：YJIT、V8 Sparkplug、JSC Baseline。
- `Tier 2` / typed SSA / LLVM：V8 TurboFan、JSC DFG/FTL、Julia、HHVM。
- deopt / safepoint / state map：Self、LLVM StackMaps/Statepoints、Graal、V8/JSC。

## 10. 后续可补充资料方向

后续如果继续深入，可以再补充这些方向的资料：

- HotSpot uncommon trap / deoptimization
- PyPy tracing JIT guard failure / resume data
- LuaJIT trace side exit
- JavaScriptCore OSR exit
- on-stack replacement paper / implementation notes
