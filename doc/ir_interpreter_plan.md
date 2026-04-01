# Baltam_IR 解释器规划

## 当前状态

当前仓库没有启用中的 IR 解释器主链。

旧的 hybrid/value-based 解释器已经不再匹配当前 IR 结构，也不再参与当前构建主线。

当前有效主线只有：

`parse -> lower(non-SSA) -> print`

## 解释器的推荐目标

后续解释器不建议直接面向当前 non-SSA IR，而建议面向：

- `untyped SSA IR`

原因是：

- SSA IR 的值流更显式
- 不再需要名字环境作为主要执行语义
- 更适合作为 profile 和优化后的统一执行层

## 解释器输入

未来解释器的输入应是：

- SSA 形式的 `Module`
- SSA 形式的 `Function`
- SSA 形式的 `BasicBlock`
- SSA 节点

而不是当前的 `NonSSANode`。

## 解释器的运行时值

当前建议仍然沿用运行时值：

```cpp
using Value = std::shared_ptr<ba_obj>;
```

也就是说，SSA 解释器的变化点在 IR 和执行模型，不在运行时对象系统。

## SSA 解释器的最小执行模型

未来最小模型建议是：

- `Frame` 维护 `SSAValue -> Value`
- 进入块时先求值 `phi`
- 普通节点按 SSA use 读取输入
- `Call` 返回一组值
- `Return` 直接返回值列表

这个模型会比旧 hybrid 解释器简单很多。

## 为什么当前不先做解释器

因为解释器的输入 IR 还没到位。

在没有下面这些前置条件前，先做解释器会把接口定早：

- CFGAnalysis
- DominatorTree
- DominanceFrontier
- Liveness
- DefUse
- BuildPrunedSSA

## 推荐顺序

1. 先完成 non-SSA IR
2. 先完成 verifier 和 analysis
3. 再完成 untyped SSA IR
4. 再做 SSA 解释器

## 当前结论

解释器仍然是长期需要的组件，但当前不应基于旧 IR 复活，也不应抢在 SSA 之前进入主线。
