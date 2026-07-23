# CFG Simplification Pass

源码位置：`src/pass/cfg_simplification_pass.cpp`

Pass 名称：`cfg-simplification`

作用域：`IRPassScope::CodeUnit`

## 优化内容

该 pass 做基础 CFG cleanup：

1. 根据 terminator 重新规范化 successor / predecessor 边
2. 删除不可达 block
3. 删除空的 goto bridge block
4. 折叠连续跳转
5. 合并单前驱 block；如果 block 以 `return` 结束且前面还有正文，也同样并入前驱

示例：

```text
entry:
  br label bridge

bridge:
  br label exit
```

可改写为：

```text
entry:
  br label exit
```

如果目标 block 以 `return` 结束且前面还有正文，也会直接并入前驱：

```text
entry:
  br label exit

exit:
  ret x
```

可改写为：

```text
entry:
  ret x
```

## 不处理的情况

- 不删除 entry block，即使 entry 是空跳转块。
- 不做条件常量折叠。
- 不做复杂 CFG restructuring。
- 不跨越会破坏 ValueTable def 指针的移动。

## 与其它 pass 的关系

`cfg-simplification` 内部会调用 `UnreachableBlockEliminationPass`。因此它适合放在
`dead-branch-elimination` 后面，先删除不可达块并合并线性 CFG，再交给后续数据流 cleanup。

当前默认 cleanup pipeline 中的使用顺序：

```text
load-forwarding
constant-folding
dead-branch-elimination
cfg-simplification
load-forwarding
constant-folding
constant-deduplication
dead-code-elimination
```

## 推荐使用场景

- lowering 后清理简单桥接 block
- 常量折叠 / 死分支消除后清理 CFG
- CodeObject 构建期最终 verify 前的 cleanup
