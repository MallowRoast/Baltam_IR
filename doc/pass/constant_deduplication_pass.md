# Constant Deduplication Pass

源码位置：`src/pass/constant_deduplication_pass.cpp`

Pass 名称：`constant-deduplication`

作用域：`IRPassScope::CodeUnit`

## 优化内容

该 pass 目前做两类优化：

1. 合并同一 `CodeUnit` 内重复的 `ConstInst`
2. 将简单自然循环中的常量移动到循环外 preheader

重复常量合并示例：

```text
%0 = const 1
%1 = const 1
%2 = add %0, %1
```

可改写为：

```text
%0 = const 1
%2 = add %0, %0
```

被删除的重复 `ConstInst` 对应 `ValueInfo::def` 会被清空，所有 use 会改写到 canonical
`ValueId`。

## 常量相等规则

当前按 `Constant` variant 的具体类型比较：

- logical / integer / char / string 按值比较
- double / complex double 按 bit 比较
- empty double matrix 之间相等

不同 `Constant` variant 即使语义上可能相等，也不会合并。例如 `1` 的 int64 和 double 表达不是同一
个常量。

## 循环常量外提

pass 会识别 lowering 生成的简单自然循环形状，把循环内部的 canonical `ConstInst` 移到唯一循环外
前驱 block 的 terminator 之前。

适合场景：

- `for` / `while` lowering 中反复出现在 header/body 的字面量
- 后续 load forwarding、DSE、常量折叠前的 canonical cleanup

## 安全边界

- 只处理 `ConstInst`，不做常量折叠。
- 不计算 `1 + 2`，也不把 dynamic operator 当 builtin。
- 循环外提只在当前 CFG 形状可识别且 verifier 可保持通过时进行。

## 推荐使用场景

- IR 打印前的 cleanup
- CodeObject 构建期优化 pipeline 的早期阶段
- 常量折叠前的 canonicalization
