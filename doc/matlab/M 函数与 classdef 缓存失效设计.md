# M 函数与 classdef 缓存失效设计

## **1\. 背景：为什么要优化 M 函数和 classdef 缓存**



当前交互式执行流程中，每次用户提交一行命令，worker 初始化逻辑都会直接移除已有缓存。M 函数、脚本和 classdef 在下一次被访问时，需要重新解析、重新构建相关运行时数据。



这种做法虽然简单，但有两个明显问题：



- 效率低。只要用户连续执行命令，即使相关 `.m` 文件没有任何变化，也会反复解析同一批 M 函数、脚本和 classdef。

- handle 类对象生命周期不符合 MATLAB 行为。`pcdata` 和 classdef 缓存中可能持有默认参数、validator、类元数据等对象。如果其中包含 handle 对象，清空缓存可能释放最后一个引用，并频繁触发 handle 类的析构逻辑或 `delete` 方法。MATLAB 不会因为用户每次输入一条命令就销毁这些未变更源码对应的缓存对象。

因此，缓存不应该在每次用户提交命令时无条件清空。更合理的策略是：缓存跨命令保留；只有当对应源文件真的发生变化时，才让相关缓存失效并重建。



## **2\. 技术难点**



设计目标是尽可能减少不必要的缓存失效，同时保持方案简单、可维护、行为可预测。



主要难点如下：



- 不能继续在每次用户命令执行前移除所有缓存。这样虽然不会复用过期源码，但会破坏缓存价值，也会频繁触发 handle 对象析构。

- 不能在每次命令执行前扫描所有已缓存文件。缓存文件数量可能较多，全量扫描会把交互式命令的固定开销放大。

- 不能在每次函数调用或 classdef 查询时都读取文件 mtime。`last_write_time()` 本身需要访问文件系统，频繁调用会带来明显开销。

- 需要同时覆盖 M 函数、脚本、局部函数、classdef 以及 classdef 局部函数。同一个 `.m` 文件可能对应多个缓存条目，源文件变化时这些条目必须一起失效。

- 方案应尽量避免后台线程、文件监听器和复杂异步状态。文件监听器存在事件丢失、平台差异、线程同步和主线程回调等维护成本，不适合作为正确性基础。

当前实现引入一个全局缓存 epoch：



- 每次用户提交一条命令时，`global_state::cache_epoch` 递增。

- 每个 M 函数/脚本缓存条目记录自己最后一次检查 mtime 时所在的 epoch。

- 每个 classdef 缓存条目同样记录自己最后一次检查 mtime 时所在的 epoch。

- 缓存查找时按需检查对应条目的源文件，而不是提前扫描所有源文件。

这样可以同时满足：



- 不再无条件清空缓存。

- 不扫描全量缓存。

- 不在同一条用户命令内重复检查同一个缓存条目的 mtime。

- 源文件变化后，下一次访问该文件相关缓存时可以及时失效。

## **3\. 技术方案以及核心数据结构**



### **3\.1 总体流程**



用户每次提交命令时，执行流程只递增缓存 epoch，并保留已有 M 函数和 classdef 缓存：



```C++
global_state::get_instance().advance_cache_epoch();

pd.clear_func_syms();
global_state::get_instance().reset_persistent();
```



`initialize_worker_env()` 不再无条件清空 `cached_pcdata_map`，也不再无条件让所有非固定 classdef 失效。完整 worker 退出、`clear functions`、显式清理缓存等路径仍然可以清空相关缓存。



缓存使用惰性检查：



1. 查找 M 函数、脚本或 classdef 缓存。

2. 根据缓存条目找到对应规范化源文件。

3. 检查该缓存条目在当前 epoch 下是否已经检查过 mtime。

4. 如果已经检查过，直接复用缓存。

5. 如果没有检查过，读取当前 mtime 并与记录值比较。

6. 如果 mtime 未变化，记录 `checked_epoch = cache_epoch`，继续复用缓存。

7. 如果 mtime 变化或读取失败，失效相关缓存条目，并按原逻辑重新解析/构建。

### **3\.2 M 函数缓存条目**



`cached_pcdata` 同时保存 `pcdata`、源文件和 mtime/epoch 信息：



```C++
using cache_epoch_t = std::uint64_t;

struct cached_pcdata {
    std::shared_ptr<pcdata> data;
    fs::path source_file;
    fs::file_time_type last_mtime;
    cache_epoch_t checked_epoch = 0;
};
```



字段含义：



- `data`：缓存的解析后 `pcdata`。

- `source_file`：规范化后的源文件路径。

- `last_mtime`：注册缓存时记录的源文件 mtime。

- `checked_epoch`：该缓存条目最近一次确认 mtime 未变化时的全局 epoch。

M 函数和脚本仍然以原有缓存键索引：



```Plain Text
.<source file>$tiao$<function name>
```



`global_state` 中维护：



```C++
std::map<std::string, cached_pcdata> cached_pcdata_map;
cache_epoch_t cache_epoch = 0;
```



`cached_pcdata_map` 是唯一的 M 函数/脚本缓存索引。当前实现没有单独的源文件反向索引表；源文件级失效通过扫描 `cached_pcdata_map` 中相同 `source_file` 的条目完成。



### **3\.3 M 函数缓存注册与查找**



注册缓存时不直接操作 map，而是统一调用：



```C++
global_state::get_instance().register_cached_pcdata(cached_name, filename, pcd);
```



该函数负责：



1. 规范化 `filename`。

2. 读取当前 `last_write_time()`；如果读取失败，则不创建长生命周期缓存。

3. 扫描 `cached_pcdata_map`，删除同一 `source_file` 但 `last_mtime` 已不同的旧条目。

4. 写入或覆盖 `cached_pcdata_map[cached_name]`，记录 `data`、`source_file`、`last_mtime` 和当前 `cache_epoch`。

查找缓存时统一走：



```C++
if (auto cache = gstate.lookup_cached_pcdata(cached_name)) {
    return cache;
}
```



`lookup_cached_pcdata()` 的行为：



1. 如果 `cached_name` 不存在，返回空指针。

2. 如果条目的 `checked_epoch == cache_epoch`，直接返回缓存的 `pcdata`。

3. 否则读取 `source_file` 当前 mtime。

4. 如果读取失败或 mtime 与 `last_mtime` 不同，则扫描 `cached_pcdata_map`，删除同一 `source_file` 的所有条目，并返回空指针。

5. 如果 mtime 未变化，则更新该条目的 `checked_epoch = cache_epoch` 并返回 `pcdata`。

因此，同一源文件变化时，该文件对应的主函数、脚本主体、局部函数、classdef 局部函数缓存会一起失效。当前实现的去重粒度是“缓存条目”：同一个 epoch 内，同一个缓存条目不会重复读取 mtime；同一源文件下不同缓存键在同一 epoch 内仍可能各自读取一次 mtime。



### **3\.4 classdef 缓存条目**



classdef 缓存由 `Classdef` 管理，不放入 `global_state::cached_pcdata_map`。当前类缓存条目为：



```C++
struct class_entry {
    meta_ptr meta;
    bool fixed = false;
    std::filesystem::path source_file;
    std::filesystem::file_time_type last_mtime;
    cache_epoch_t checked_epoch = 0;
};
```



`Classdef` 内部维护：



```C++
using map_t = std::unordered_map<std::string, class_entry>;
map_t m_classdefs;
```



注册类时，如果存在稳定源文件路径，则规范化路径并记录 mtime：



```C++
Classdef::get_instance().register_class(meta, filename, true);
```



查找类时：



1. 固定类直接返回。

2. 如果元数据已经标记无效，返回空指针。

3. 如果传入的源文件与缓存记录不一致，标记旧元数据无效并返回空指针。

4. 如果没有稳定源文件路径，标记无效并返回空指针。

5. 如果 `checked_epoch == global_state::cache_epoch`，直接返回缓存元数据。

6. 否则读取 classdef 源文件 mtime。

7. mtime 读取失败或发生变化时，标记元数据无效并返回空指针。

8. mtime 未变化时，更新 `checked_epoch` 并返回缓存元数据。

固定 classdef、builtin classdef 或没有稳定文件路径的 classdef 不参与普通 mtime 持久缓存检查。



classdef 文件中的普通方法/局部函数仍会通过 `register_cached_pcdata()` 写入 `cached_pcdata_map`，因此方法 `pcdata` 缓存和 classdef 元数据缓存是两套结构，但使用同一个 `cache_epoch` 节流检查。



对于继承关系，当前缓存复用依赖类查找路径按需检查相关 classdef 条目。父类文件变化会使父类元数据失效；子类是否随之失效取决于后续类解析/查找路径对依赖类的访问和重建。



### **3\.5 源文件规范化**



源文件路径必须先规范化，避免同一个文件因为相对路径、符号链接或不同字符串形式产生多份缓存记录：



1. 使用 `std::filesystem::u8path` 构造路径。

2. 优先使用 `weakly_canonical(path, ec)`。

3. 如果失败，退回 `absolute(path, ec)`。

4. 如果仍然失败，使用原始 UTF\-8 路径字符串。

mtime 通过 `last_write_time(path, ec)` 读取。如果注册缓存时无法读取 mtime，则不创建长生命周期缓存条目，避免无法判断源变化的缓存被错误复用。



### **3\.6 源文件检查与失效**



M 函数/脚本缓存的检查集中在 `global_state::lookup_cached_pcdata()` 中。



```C++
std::shared_ptr<pcdata> global_state::lookup_cached_pcdata(const std::string& key) {
    auto it = cached_pcdata_map.find(key);
    if (it == cached_pcdata_map.end()) return nullptr;

    auto& entry = it->second;
    if (entry.checked_epoch == cache_epoch) {
        return entry.data;
    }

    fs::file_time_type mtime;
    if (!read_source_mtime(entry.source_file, mtime) || mtime != entry.last_mtime) {
        for (auto it = cached_pcdata_map.begin(); it != cached_pcdata_map.end();) {
            if (it->second.source_file == entry.source_file) {
                it = cached_pcdata_map.erase(it);
            } else {
                ++it;
            }
        }
        return nullptr;
    }

    entry.checked_epoch = cache_epoch;
    return entry.data;
}
```



M 函数/脚本缓存按源文件统一失效：



- mtime 变化或读取失败时，删除 `cached_pcdata_map` 中同一 `source_file` 的所有条目。

- `clear_cached_pcdata()` 直接清空 `cached_pcdata_map`。

- `global_state::clear()` 会清空全局符号表、`cached_pcdata_map` 和 `persistent_vars`。

- `clear functions` 会清空当前工作区函数符号、`cached_pcdata_map`、`persistent_vars`，并软清理 BEX。

classdef 缓存失效由 `Classdef::lookup_class()` 和 `Classdef::register_class()` 管理：



- classdef 源文件 mtime 变化或读取失败时，将对应元数据标记为无效。

- 源文件路径不一致时，将旧元数据标记为无效。

- `clear classes` 或显式 classdef invalidation 路径仍可批量失效类元数据。

失败语义：



- 源文件缺失：M 函数/脚本缓存删除同源文件条目；classdef 元数据标记无效。

- `last_write_time()` 读取失败：M 函数/脚本缓存删除同源文件条目；classdef 元数据标记无效。

- M 函数/脚本缓存注册时读取 mtime 失败：不写入 `cached_pcdata_map`。

- 内建源、内存源、没有稳定文件路径的源：不进入 mtime 持久缓存，遇到时按原逻辑重建或使用固定缓存策略。

## **4\. 测试方案**



### **4\.1 M 函数缓存复用**



构造普通 M 函数：



1. 第一次调用函数，确认会解析并写入缓存。

2. 不修改文件，连续执行多条用户命令调用该函数。

3. 验证函数不会被重复解析，缓存对象能跨命令复用。

4. 验证同一条用户命令内多次调用该函数时，同一个缓存条目不会重复检查 mtime。

### **4\.2 M 函数文件变化后失效**



1. 调用 M 函数并建立缓存。

2. 修改对应 `.m` 文件内容，确保 mtime 变化。

3. 再次调用函数。

4. 验证旧缓存失效，并使用新文件内容重新解析和执行。

5. 如果同一个文件包含主函数和局部函数，验证它们对应的缓存键会一起失效。

### **4\.3 脚本和局部函数缓存**



1. 构造脚本文件和包含多个局部函数的 M 文件。

2. 验证脚本主体缓存、主函数缓存和局部函数缓存都能记录相同的规范化源文件路径。

3. 修改源文件后，验证该文件关联的所有缓存条目一起失效。

### **4\.4 handle 默认值生命周期**



构造带 handle 默认参数或 validator 默认值的 M 函数：



1. 第一次调用后建立缓存，并记录 handle 对象析构次数。

2. 不修改源文件，连续提交多条命令。

3. 验证缓存未被无条件清空，handle 析构不会因为每条命令执行而反复触发。

4. 修改源文件后再次调用，验证旧缓存被释放，析构行为只发生在源文件真实失效时。

### **4\.5 classdef 缓存复用和失效**



构造普通 classdef：



1. 第一次访问类，确认类元数据被构建并写入缓存。

2. 不修改 classdef 文件，连续执行多条命令访问该类。

3. 验证 classdef 缓存跨命令复用。

4. 修改 classdef 文件后再次访问，验证旧类缓存失效并重建。

### **4\.6 classdef 继承关系**



构造父类和子类：



1. 建立父类、子类缓存。

2. 不修改文件时，验证两者都可以复用缓存。

3. 修改父类文件。

4. 再次访问子类，验证父类变化会使子类相关缓存失效或进入待重建状态。

### **4\.7 cache epoch 行为**



添加计数或日志验证：



1. 每次用户提交命令时，`cache_epoch` 递增一次。

2. 同一条用户命令内，多次访问同一个缓存条目，只调用一次 `last_write_time()`。

3. 同一条用户命令内，访问同一源文件的不同缓存键时，当前实现允许各缓存条目分别检查 mtime。

4. 下一条用户命令中再次访问同一缓存条目，会重新检查一次 mtime。

### **4\.8 异常和边界场景**



覆盖以下场景：



- 源文件被删除后再次访问，缓存应失效并报出合理错误或重新走解析路径。

- `last_write_time()` 失败时，缓存应失效，不能继续复用旧缓存。

- 相对路径、绝对路径、符号链接路径指向同一文件时，应尽量归一到同一个规范化源文件路径。

- builtin 或没有稳定文件路径的对象不应进入普通 mtime 持久缓存。

- `clear functions`、完整 worker teardown 等显式清理路径仍然会清空相关缓存。
