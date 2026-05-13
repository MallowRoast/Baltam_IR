# 高维数组规约运算设计思考

## 1. 背景与目标

规约运算是 MATLAB 风格数组计算中的高频基础操作，例如：

```matlab
sum(A, dim)
prod(A, dim)
max(A, [], dim)
min(A, [], dim)
all(A, dim)
any(A, dim)
mean(A, dim)
```

对于高维数组：

```matlab
A = rand(2, 3, 4, 5, 6, 7);
B = sum(A, 2);
C = any(A, [2 5 7]);
```

接口看起来简单，但实现上会受到规约维度、shape、数据类型、算子语义和执行后端的共同影响。多数规约算子本身计算量不高，性能往往取决于内存访问顺序、cache/SIMD 利用率、临时数组数量和后端调度方式。

高维规约的核心难点可以分为三类：

- 单一维度规约：可以抽象为 `frontDim / midDim / rearDim`，但退化 shape、遍历顺序和 `all/any` 短路会影响 kernel 选择。
- 多维度规约：规约维度可能不连续，需要在连续维度合并、多次单维规约和 fused N-D reduce 之间选择。
- 多后端执行：CPU/GPU 应共享同一套 logical kernel plan，但使用各自适合硬件的调度方式。

整体目标是建立如下分层：

```text
Frontend API
    fun(A, dims, op)

Planner
    shape + dims + op -> KernelPlan / KernelPipeline

Backend Scheduler
    CPUBackend.execute(plan)
    CudaBackend.execute(plan)
```

Planner 负责算法规划，Backend 负责执行调度。这样可以避免 CPU 和 CUDA 各自维护一套分裂的规约系统。

## 2. 单一维度规约问题

对于：

```matlab
fun(A, dim)
```

可以把高维数组逻辑上视为三维数组：

```cpp
frontDim = prod(shape[0 : dim])
midDim   = shape[dim]
rearDim  = prod(shape[dim + 1 : end])
```

列主序下，输入和输出访问形式为：

```cpp
A[k * frontDim * midDim + j * frontDim + i]
C[k * frontDim + i]
```

其中：

```cpp
i in [0, frontDim)
j in [0, midDim)
k in [0, rearDim)
```

因此，单维规约可以统一为：

```text
for k in rearDim
    for i in frontDim
        reduce over j
```

### 2.1 退化维度

通用三重循环不适合所有 shape。Planner 应优先识别快路径：

```cpp
if (midDim == 1) {
    CopyOrCast
} else if (frontDim == 1) {
    ContiguousReduce
} else {
    GeneralReduce
}
```

其中：

- `midDim == 1`：规约退化为 copy 或 cast；
- `frontDim == 1`：规约维度在内存上连续，可以直接走连续规约；
- `rearDim == 1`：输出块较少，需要注意并行粒度和调度开销。

### 2.2 遍历顺序

自然顺序是：

```cpp
for k
    for i
        for j
```

它适合 `all/any`，因为每个输出元素可以独立提前退出；但 `j` 方向访问步长为 `frontDim`，当 `frontDim` 较大时缓存不友好。

缓存友好的顺序是：

```cpp
for k
    for j
        for i
```

此时 `i` 是内层循环，访问连续，更适合 cache、SIMD 和 GPU coalescing；但 `all/any` 不容易对单个输出元素直接 `break`。

### 2.3 分派策略

单维规约不应只有一个通用 kernel。更合理的分派是：

```cpp
if (midDim == 1) {
    CopyOrCast
} else if (frontDim == 1) {
    ContiguousReduce
} else if (!isShortCircuitOp(op)) {
    TiledReduce
} else {
    BlockedShortCircuitReduce
}
```

对于 `sum/prod/min/max`，通常使用 `k-j-i` 或 tiled reduce，因为必须扫描完整规约维度。

对于 `all/any`，CPU 后端可以使用 blocked short-circuit：

```text
for k
    for ib in frontDim by Block
        init result[Block], done[Block]

        for j
            scan contiguous i block
            update result/done

            if all entries in block are done
                break
```

这个方案在局部连续访问和短路能力之间折中。

## 3. 多维度规约问题

对于：

```matlab
fun(A, [2 5 7])
```

单个 `frontDim / midDim / rearDim` 已经不够。多维规约需要先规划维度，再选择执行策略。

### 3.1 连续维度合并

如果规约维度连续，例如：

```cpp
reduceDims = [2, 3, 4]
```

可以合并成一个大的 `midDim`：

```cpp
frontDim = prod(shape before 2)
midDim   = shape[2] * shape[3] * shape[4]
rearDim  = prod(shape after 4)
```

如果规约维度为：

```cpp
reduceDims = [2, 3, 4, 7]
```

可以合并为连续段：

```text
[2..4], [7]
```

然后生成一条规约 pipeline。连续维度合并可以减少 kernel 数量和中间数组数量，同时复用单维规约 kernel。

### 3.2 多次单维规约 + Tensor View

对于不连续规约维度，例如：

```cpp
reduceDims = [2, 5, 7]
```

baseline 方案是多次单维规约：

```cpp
tmp1 = reduce(A, 7)
tmp2 = reduce(tmp1, 5)
C    = reduce(tmp2, 2)
```

通常从高维到低维规约，维度编号更容易维护。中间结果尽量用 tensor view / matrix view 描述 shape、stride 和逻辑维度，避免不必要的物理 reshape。

这个方案实现简单、复用性好，也适合 `all/any`，代价是可能产生临时数组和多次内存读写。

### 3.3 Fused N-D Reduce

Fused N-D reduce 一次性扫描输入：

```cpp
for each input element:
    outIndex = project(inputIndex, nonReducedDims)
    C[outIndex] = reduce(C[outIndex], A[inputIndex])
```

它适合 `sum/prod/min/max` 这类非短路规约，优点是输入连续扫描且不需要中间数组。主要代价是输出写入可能不连续，并行后端需要处理多个执行单元写同一输出的问题。

常见并行实现方式包括：

- atomic update；
- 每个线程组生成 partial output；
- 第二阶段合并 partial output；
- 对小输出使用 shared/local buffer。

多维规约可以按如下策略规划：

```cpp
if (reduceDims are contiguous) {
    ContiguousReduce
} else if (!isShortCircuitOp(op)) {
    FusedNDReduce or MultiPassReduce
} else {
    MultiPassReduce
}
```

## 4. 规约问题多后端架构设计

CPU/GPU 应被视为计算后端，而不是算法分派入口。统一 Planner 根据 shape、dims 和 op 生成 logical kernel plan；不同后端执行同一个 plan。

```cpp
enum class ReduceKernelKind {
    CopyOrCast,
    ContiguousReduce,
    StridedReduce,
    TiledReduce,
    BlockedShortCircuitReduce,
    MultiPassReduce,
    FusedNDReduce,
};
```

Plan 描述逻辑计算模式：

```cpp
struct ReduceKernelPlan {
    ReduceKernelKind kind;
    ReduceOp op;

    Shape inputShape;
    Shape outputShape;
    std::vector<int> reduceDims;

    baSize frontDim;
    baSize midDim;
    baSize rearDim;

    TileConfig tile;
    bool allowShortCircuit;
};
```

执行接口可以抽象为：

```cpp
ReduceKernelPlan plan = make_reduce_plan(shape, dims, op);
backend.execute(plan, input, output);
```

这里的“同一个 kernel”指同一个 logical kernel，而不是强行复用同一段外层循环代码。

### 4.1 CPU Backend

CPU 后端关注：

- cache locality；
- SIMD；
- thread pool；
- blocked short-circuit；
- small shape overhead。

例如 `ContiguousReduce` 可以由线程池切分输出段，在每段内部使用 SIMD 或标量循环。`all/any` 可以保留积极的短路策略，因为单个输出提前结束能直接减少访存。

### 4.2 CUDA Backend

CUDA 后端关注：

- grid/block/warp 映射；
- coalesced access；
- shared memory；
- warp/block reduce；
- multi-stage reduce；
- occupancy。

例如 `ContiguousReduce` 可以由一个 warp 或 block 处理一个输出段。`TiledReduce` 应尽量让相邻线程访问相邻 `i`，保证 coalesced load。

对于 `all/any`，CUDA 第一版通常可以退化为完整规约：

```cpp
any: acc = acc || predicate(x)
all: acc = acc && predicate(x)
```

只有当 `midDim` 很大且短路概率极高时，才考虑 block 内短路或两阶段短路规约。

### 4.3 后端关系

同一个 logical kernel 在不同后端上可以有不同调度：

```text
ContiguousReduce:
    CPU: thread + SIMD
    CUDA: warp/block reduce

TiledReduce:
    CPU: cache blocking
    CUDA: coalesced load + block/warp reduction

BlockedShortCircuitReduce:
    CPU: preserve all/any early exit
    CUDA: usually lower to full reduction

FusedNDReduce:
    CPU: input-order scan
    CUDA: atomic, partial output, or multi-stage reduction
```

最终目标是：统一 Planner 负责算法分派，不同 Backend 负责执行调度。这样既能保证规约语义和规划逻辑一致，又能为 CPU/GPU 保留各自的优化空间。
