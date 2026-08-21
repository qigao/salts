# TurboSTL、CMeta 与 CFlow 一体化设计

日期：2026-08-21

## 背景

仓库当前存在三套相互重叠的能力：

- `utils/`（`TurboUtils::Core`）编译并安装一组基础容器；
- 旧 `turbo/` 目录包含更完整的容器集合及 typed 宏；
- 旧 `stream/` 提供一套独立的 Java 风格有限流和 live/SPSC 运行时，而 `cflow/` 已经拥有 typed graph、runtime、source、scheduler 与 fluent stream facade。

当前工作区已经开始删除旧 `turbo/`、`stream/` 并建立 `turbostl/`，但尚未完成 target、安装、测试、调用方和错误/所有权协议的迁移。本设计以这些未提交改动为迁移基线，不恢复第二套容器或 Stream runtime。

## 目标

1. 将 Core 中的标准容器和旧 `turbo/` 容器统一迁入 `turbostl/`，使其成为唯一标准容器库。
2. 以 CMeta `typed(...)`、`Containers(...)` 和显式 traits 生成 typed facade、Range 与 collector 能力。
3. 由 TurboSTL 提供 CFlow 的容器实现适配，使有限容器获得 Java 风格 lazy pipeline 与 terminal API。
4. 保持 CFlow 不依赖具体 TurboSTL；STLCFlow 在构造 stream 时显式注入状态后端和 collector。
5. 对所有增长状态设置硬上限，明确所有权、失效点、错误、清理与重复执行语义。
6. 删除旧公开 include、typed 宏和 `TurboUtils::Stream` target，不保留兼容层。

## 非目标

- 不复刻旧 `stream_live`、`stream_spsc`、window、count/time debounce、snapshot 或 C++ Stream wrapper。
- 不在本次增加 parallel/unordered stream。
- 不让 CMeta 实现容器分配、hash probing、树平衡、排序或 CFlow runtime。
- 不让 CFlow include、链接或运行时发现 Turbo 容器。
- 不提供旧 header、target 或宏的 deprecated forwarding layer。
- 不改变持久化数据格式；本次只有源码、二进制 ABI 和构建 target 的 breaking change。

## 候选方案与决策

### 方案 A：分层单事实源（采用）

标准容器、CFlow 抽象和二者适配各有独立 target。CMeta 承载有限的类型与能力描述，STLCFlow 通过显式 Adapter/Strategy 将 TurboSTL 接入 CFlow。

优点是无依赖环、无全局注册、无算法复制，Core 也能只消费标准容器本体。代价是新增一个明确的 `TurboUtils::STLCFlow` target，并需要一次完整的 breaking migration。

### 方案 B：CFlow 直接依赖 TurboSTL（拒绝）

实现状态算子较直接，但违反“TurboSTL 是 CFlow 实现、CFlow 不感知 TurboSTL”的边界，并会把具体容器依赖扩散到通用 graph/runtime。

### 方案 C：由 typed 容器宏直接生成 Stream 算法（拒绝）

表面调用短，但会复制 CFlow 的执行、错误、优化和生命周期语义，增加头文件编译成本，形成第二套事实源。

## 模块与 target 依赖

以下箭头表示“左侧依赖右侧”：

```text
TurboUtils::CFlow          -> TurboUtils::CMeta
TurboUtils::STL      -> TurboUtils::CMeta
TurboUtils::STLCFlow -> TurboUtils::STL + TurboUtils::CFlow
TurboUtils::Core           -> TurboUtils::STL（PRIVATE）
```

职责如下：

- `TurboUtils::CMeta`：类型描述、type traits、Range、container descriptor 与 collector 协议。
- `TurboUtils::CFlow`：graph、operator、terminal、runtime、status 和抽象的有界状态后端协议。
- `TurboUtils::STL`：标准容器算法、raw API、typed facade、Range/collector adapter。
- `TurboUtils::STLCFlow`：TurboSTL 对 CFlow stream、state backend 和 typed collector 的实现。
- `TurboUtils::Core`：错误、字符串、文件、平台、日志、automata 等共享能力；不再拥有或安装标准容器。

TurboSTL 不依赖 Core。它提供自己的 export header 与 `turbo_stl_status`，避免 Core 私有消费 TurboSTL 时产生依赖环。Core 在自身公开边界把 TurboSTL 错误转换为现有 `TURBO_E*`。

## 文件布局

```text
cmeta/include/cmeta/
  type_traits.h
  container.h
  range.h
  collector.h

cflow/include/cflow/
  status.h
  backend.h
  operators.h
  stream.h
  terminals.h
  collectors.h

turbostl/
  CMakeLists.txt
  include/turbo/
    stl.h
    stl/
      export.h
      status.h
      vec.h
      deque.h
      list.h
      stack.h
      queue.h
      heap.h
      set.h
      hash_set.h
      hash_map.h
      map.h
      multimap.h
      btree.h
      bplus_tree.h
      typed.h
      cflow.h
  src/
    list.c
    deque.c
    rb_tree.c
    hash_table.c
    map.c
    set.c
    multimap.c
    hash_map.c
    hash_set.c
  cflow/
  tests/
  examples/
  benchmarks/
```

`<turbo/stl.h>` 是标准容器聚合头；`<turbo/stl/cflow.h>` 是可选 CFlow 集成入口。旧 flat include 路径不安装。

## 标准容器范围

迁入 TurboSTL 的标准容器为：

```text
Vec / Deque / List / Stack / Queue / Heap
Set / HashSet / HashMap / Map / MultiMap
BTree / BPlusTree
```

同时迁移对应源码、typed 生成、测试、examples 和 benchmark。Core 中以下能力不属于标准容器，继续留在原模块：

```text
turbo_buffer / memory_pool / object_pool
ring_buffer / ring_buffer_spsc / disruptor
bucket_priority_queue / bucket_priority_queue_spsc / bucket_priority_queue_mpmc
threadpool / coro_pool
```

Core 内的 `ac_automaton`、`levenshtein_automaton` 以及 `turbo_serial` 等调用方改用新 include 和 target，不复制或私藏 Vec 实现。

### 容器名称与数据结构契约

容器名称直接表达数据结构和可观察语义，不允许用另一个容器的薄别名实现：

| 公开容器 | 唯一事实源 | 顺序 | 核心复杂度 |
|---|---|---|---|
| `List` | 独立双向链表节点 | 插入顺序 | 已知节点处插入/删除 `O(1)`，查找 `O(n)` |
| `Deque` | 循环数组 | 逻辑索引顺序 | 两端操作摊销 `O(1)`，随机访问 `O(1)` |
| `Map` | 红黑树 | key 升序、key 唯一 | 查找/插入/删除 `O(log n)` |
| `Set` | 红黑树 | value 升序、value 唯一 | 查找/插入/删除 `O(log n)` |
| `MultiMap` | 红黑树 | key 升序；同 key 按插入顺序 | 插入 `O(log n)`；key range `O(log n + k)` |
| `HashMap` | 开放寻址 hash table | 未指定 | 平均查找/插入/删除 `O(1)`，最坏 `O(n)` |
| `HashSet` | 开放寻址 hash table | 未指定 | 平均查找/插入/删除 `O(1)`，最坏 `O(n)` |

`List` 不包含 `turbo_deque_t`，不公开 `reserve`、`capacity` 或随机访问 `at(index)`。它公开双向 iterator、`insert_before/after` 和 `erase`；插入不使既有节点地址失效，删除只使被删除节点失效，`clear/destroy` 使全部节点失效。

`Map` 不 typedef、不组合 `turbo_hash_map_t`，也不接收 hash/equal。它从 key type traits 取得 comparator，并公开 `lower_bound`、`upper_bound`、有序 iterator 和唯一 key 的替换语义。`Set` 复用同一内部红黑树引擎但没有 value。`MultiMap` 的内部比较键为 `(user_key, insertion_sequence)`，从而让相同 key 连续且保持插入顺序；sequence 溢出必须在修改前失败。

`HashMap` 与 `HashSet` 复用内部开放寻址引擎，要求 hash/equal traits，最大占用率为 70%，显式处理 collision、tombstone 和 rehash。rehash 使全部 borrowed pointer 失效。哈希槽编号只是实现细节，不作为 public iterator、Range 或持久化顺序。

`BTree`、`BPlusTree` 保持独立算法和公开类型；不得以 B-tree 实现或名称替代红黑树 Map。

### 红黑树不变量

内部 `rb_tree.c` 只服务 `Map`、`Set` 和 `MultiMap`，不作为运行时可切换 backend。每次成功修改后必须同时满足：

- root 为黑色；
- 红节点没有红色子节点；
- 任一节点到叶哨兵的黑高相同；
- `Map/Set` 中序 key 严格递增；`MultiMap` 中序复合键严格递增；
- parent/left/right 互相一致，记录的 size 等于实际节点数。

rotation 只重连节点，不移动节点内 key/value payload。插入必须先完成节点和 payload 构造再挂入树；删除带输出时先校验 move trait 与未初始化输出契约，再修改树结构并 move payload。失败操作不得改变 root、size、generation 或任一已有 payload。

## Breaking public API

- raw 类型名如 `turbo_vec_t`、`turbo_hash_map_t` 保留。
- raw API 改用 TurboSTL 自身 status 和显式类型/字节语义，不再从 Core 取得错误码。
- typed 声明的唯一公开入口是 `typed(Kind, ...)` 和 `Containers(...)`。
- `TURBO_VEC_DEFINE`、`TURBO_HASH_MAP_DEFINE` 等宏降为 TurboSTL 内部生成机制，不再安装或文档化。
- include 统一迁移至 `<turbo/stl/...>`。
- 删除 `TurboUtils::Stream`；普通用户链接 `TurboUtils::STL`，容器流用户链接 `TurboUtils::STLCFlow`。
- 删除 List 的 `reserve/capacity/at` 和所有“deque-backed list”契约。
- Map 初始化由 hash/equal 改为 compare trait；删除 hash slot/capacity API 并改为有序 iterator/bounds API。
- Set 与 MultiMap 迁移到有序红黑树族；HashSet 与 HashMap 保留哈希语义。
- raw 与 typed `init/from` 都显式接收 `max_elements`；`0` 表示合法但不可增长的空容器，不表示无界。

对 raw byte 容器，初始化 API 必须显式声明 trivial byte 语义；typed 容器则使用 CMeta type descriptor。禁止把带资源所有权的值隐式当作可 `memcpy` 字节。

### List、Map 与 HashMap 公开能力

公开头只暴露语义能力，不暴露 node、rotation、probe 或 tombstone 操作：

```text
List
  init / init_bytes / from / destroy / clear
  push_front / push_back / pop_front / pop_back
  begin / end / next / prev / value
  insert_before / insert_after / erase
  front / back / size / empty / generation

Map
  init / init_bytes / from / destroy / clear
  put / get / contains / remove
  begin / end / next / prev / key / value
  lower_bound / upper_bound / size / empty / generation

HashMap
  init / init_bytes / from / destroy / clear
  reserve / put / get / contains / remove
  begin / end / next / key / value
  size / capacity / empty / generation
```

`init` 使用 type descriptor/traits；`init_bytes` 只接受显式 size/alignment 和 comparator 或 hash/equal，且只能保存 trivial byte 值。List/Map iterator 保存 owner 和当前节点；插入与 rotation 不使它失效，erase 只使指向目标节点的 iterator 失效。失效节点 iterator 不可安全自检，继续使用属于调用方契约违例。HashMap iterator 保存 owner、当前 slot 和捕获 generation，任何 rehash 后访问都返回 `TURBO_STL_INVALID_ARGUMENT`。iterator 都是 borrowed handle，不拥有容器或 payload。

`MultiMap` 不提供含糊的 `remove(key)`：`erase(iterator)` 删除一个精确 entry，`erase_key(key)` 删除该 key 的全部 entries，`equal_range(key)` 返回同 key 的有序区间。任何批量删除在开始前完成参数与 trait 校验，执行阶段不包含可失败分配。

## CMeta type traits

`cmeta_type_desc` 关联一份显式 traits：

```c
typedef struct cmeta_type_traits {
    cmeta_trait_flags flags;
    bool (*equal)(const void *left, const void *right);
    uint64_t (*hash)(const void *value);
    int (*compare)(const void *left, const void *right);
    bool (*copy_construct)(void *dst, const void *src);
    void (*move_construct)(void *dst, void *src);
    void (*destroy)(void *value);
} cmeta_type_traits;
```

内建标量由 CMeta 提供完整 traits。显式标为 trivially copyable/destroyable 的类型使用 byte copy/no-op destroy。自定义拥有型类型必须注册 copy/move/destroy；Map/Set/MultiMap 要求 compare，HashMap/HashSet 与 distinct 要求 hash+equal，排序要求 compare。

traits 和 descriptor 遵循现有 multi-TU 规则：header-local descriptor 地址不是全局 type identity，消费者使用语义字段和 `cmeta_type_equal`，不比较地址。

缺少能力在 typed 声明、构图或 collector admission 的最早可判定边界失败。禁止自动使用结构体 padding 上的 `memcmp`、原始 byte hash、地址比较或浅拷贝作为自定义类型 fallback。

## CMeta container 与 collector traits

`cmeta_container_desc` 分为只读与写入能力：

```text
read capability
  default / keys / values / entries Range factories

collect capability
  collector begin / accept / finish / abort
```

`typed(Vec, IntVec, int)` 或一行 `Containers(...)` 一次生成：

- typed wrapper 和 forwarding functions；
- container/type descriptor；
- element/key/value ownership metadata；
- Range factories 与 range flags；
- collector factory；
- TurboSTL 内部 raw adapter。

这些产物由稳定的 TurboSTL schemas 经 CMeta `Schema/Replay` 生成。方法表、descriptor、Range 与 collector 不各自维护重复的容器种类列表。CMeta 只生成 metadata 和薄 facade，实际算法仍由 TurboSTL 编译库拥有。

## TurboSTL 值所有权

- 插入拥有型值时执行 copy/move construct；失败不得留下半初始化 slot。
- 移除到调用方输出时执行 move construct；无输出时直接 destroy。
- clear/destroy 必须对每个活元素恰好调用一次 destroy。
- key/value 分别使用各自 traits；替换 Map value 时先构造新值，再原子地替换并销毁旧值。
- `get/at/front/back` 与 iterator value 返回 borrowed pointer。Vec/Deque/HashMap 的 reserve/rehash 使相关地址失效；List 和红黑树的插入不移动既有节点 payload，erase 只使目标节点失效；clear/destroy 始终使全部借用失效。
- raw byte 初始化只允许 trivial 类型，不接管指针指向资源。

每个 raw 容器维护 mutation generation。Range 创建时捕获 generation，每次读取前校验；通过公开容器 API 发生的中途 mutation 必须返回执行错误。直接写入已借出的 mutable pointer 仍是调用方契约违例，无法由 generation 完整检测。

List 和红黑树不能通过 ordinal `size_t` 在总计 `O(n)` 内完成遍历。CMeta Range 因此使用 opaque cursor：

```c
typedef struct cmeta_range_cursor {
    size_t index;
    void *state[2];
} cmeta_range_cursor;
```

数组和 hash range 使用 `index`，List/RB tree range 使用 `state` 保存当前节点。cursor 由调用方零初始化，只能交回创建它的 Range；不得复制到另一个 owner 或在 mutation 后继续使用。`cmeta_range_next_fn`、CFlow range source 和生成的 TurboSTL adapters 同步迁移到该类型，不保留把节点指针塞进 `size_t` 的平台假设。

所有容器是 single-threaded。跨线程共享必须由调用方在容器 API 外同步；`const` 查询与 iterator 不隐含线程安全。初始化记录 `max_elements` 硬上限，所有节点大小、payload offset、alignment、capacity 和总字节计算都使用 checked arithmetic。到达上限返回 `TURBO_STL_CAPACITY_EXCEEDED`，OOM 返回 `TURBO_STL_OUT_OF_MEMORY`，二者不得混淆。

`init(..., max_elements)` 创建空容器；`from(..., count, max_elements)` 要求 `count <= max_elements`，失败时输出保持零状态。API 不提供“0 等于无限”或自动扩展上限的 fallback。HashMap 的 `capacity` 是 `max_elements` 约束下的内部 slot 预算，List/RB tree 不公开 capacity 概念。

List/RB tree 初版使用单节点分配，不依赖 Core 的 pool，也不预先加入 allocator/pool 策略层。只有 benchmark/profile 证明节点分配是相关 workload 的瓶颈后，才单独设计可注入 allocator；池化不得改变节点稳定性、上限或失败原子性。

## CFlow 容器适配边界

CFlow 定义两个小型状态接口：

```text
cflow_sequence_state_ops
  begin / append / stable_sort / iterate / destroy

cflow_set_state_ops
  begin / insert_if_absent / destroy
```

STLCFlow 以 Adapter + Strategy 实现这些接口：sequence state 使用 `turbo_vec_t`，distinct state 使用 `turbo_hash_set_t`，typed terminal 使用 CMeta collector。

不使用全局 registry、服务定位器或隐式弱 fallback。backend 由 `<turbo/stl/cflow.h>` 在构造 stream 时显式注入。CFlow graph 只保存语义参数，不保存 TurboSTL 对象：

```c
cflow_stream stream = {0};
stream(&values, &stream)
    ->filter(&stream, even)
    ->map(&stream, square);
```

`stream_keys`、`stream_values`、`stream_entries` 对关联容器选择显式 view；Map 不定义模糊的默认元素流。普通 CFlow Range 继续使用 CFlow 自身的 range/source 构造 API，不需要 TurboSTL。

没有 backend 时，无状态 CFlow pipeline 正常工作；请求 distinct、sorted 或 typed collection 时返回 `CFLOW_UNSUPPORTED`。该结果是显式能力检查，不触发 interpreter/container fallback。

## Java 风格操作面

Lazy intermediate operations：

```text
filter
map
flatMap
peek
limit / take
skip
takeWhile
dropWhile
distinct
sorted
concat
transform
zip
```

Terminal operations：

```text
forEach
toArray
collect
count
reduce
findFirst / findAny
anyMatch / allMatch / noneMatch
contains
min / max
groupingBy
partitioningBy
```

`CFlowOperators` 只描述 graph 节点。独立的 `CFlowTerminals` schema 生成 terminal method typedef、成员、声明与 dispatch。现有 `REDUCE` primitive 继续作为内部执行 IR，公开 `reduce` 是 Java 语义 terminal，不再返回可继续串联的 stream。

容器流是确定性顺序流：`findAny` 等同 `findFirst`；distinct 稳定保留首次出现值；sorted 对有 encounter order 的输入执行稳定排序；take/drop while 保持输入顺序；peek 标为 effectful，优化器不得跨越、删除或重排。

示例：

```c
cflow_stream stream = {0};
IntVec output = {0};

cflow_status status =
    stream(&values, &stream)
        ->filter(&stream, even)
        ->map(&stream, square)
        ->distinct(&stream, max_unique)
        ->sorted(&stream, max_items)
        ->collect(&stream, IntVec_collector(&output), max_output);
```

同一 graph 可重复执行。terminal 不消费或修改 graph；只要 source container 未修改且仍存活，每次 terminal 都创建新的 cursor、run state 和 collector transaction。

## Graph 参数与执行后端

参数化 operator 使用 typed node parameter union 保存 `limit.count`、`skip.count`、`distinct.max_unique`、`sorted.max_items` 等数据，不使用字符串键或裸 `void *` 承载核心状态。

normalize/optimize 复制并验证 typed 参数。backend interface 通过 stream/eval options 借用，不写入语义 graph。显式 Graph API 在运行状态算子时也必须提供 backend options。

compiled plan 支持范围可以窄于 runtime。`cflow_plan_graph_supported` 必须拒绝尚未实现的状态算子；plan compile 失败不会隐式转 interpreter。Surface、normalized、optimized 和 eligible compiled plan 的 observable values、顺序、数量、类型、错误和终态必须一致。

## 数据路径与有界资源协议

```text
source container（唯一事实源，borrowed）
    -> Range cursor
    -> CFlow-owned transient values
    -> bounded backend state
    -> terminal collector transaction
    -> committed output
```

有限容器执行拓扑是 single-threaded、单 source、单 terminal consumer。source container、Range descriptor 和 backend interface 由调用方借用；run state、transient values 和未提交 collector state 由当前 terminal evaluation 拥有。

所有可增长状态必须接收元素数量硬上限，并在分配前检查：

```text
required_bytes = aligned_element_size * element_limit + metadata_bytes
```

乘法、加法和对齐全部使用 checked arithmetic。到达上限立即返回 `CAPACITY_EXCEEDED`；不阻塞、不丢弃、不覆盖、不自旋、不转为无界分配。

至少以下操作必须显式提供上限：distinct、sorted、flatMap materialization、toArray、collect、groupingBy、partitioningBy。grouping/partition 同时限制最终 bucket 数、总元素数和 retained payload。

Collector 状态机为：

```text
ZERO -> BEGUN -> ACCEPTING -> COMMITTED
                     +-----> ABORTED
```

每次成功 begin 必须恰好 commit 或 abort。输出对象进入 terminal 前必须为零初始化且不拥有资源。失败时 collector 销毁全部已构造值并恢复零状态；source 和 graph 不变，pipeline 可再次执行。

短路 terminal 在结果确定后取消 run、停止拉取 source，并释放全部 transient state。Range 返回的 borrowed value 只在完成当前 copy/move 前有效，不跨 callback、下一次 `next`、mutation 或 terminal 返回保存裸指针。

## 算法语义与复杂度

- List：已知 iterator 处插入/删除 `O(1)`，按值或位置查找 `O(n)`，完整迭代 `O(n)`；不提供随机访问伪装。
- Map/Set：查找、插入、删除和 bounds `O(log n)`，完整中序迭代 `O(n)`，空间 `O(n)`。
- MultiMap：按 key 定位 `O(log n)`，枚举同 key 的 `k` 个值为 `O(k)`；同 key 保持插入顺序。
- HashMap/HashSet：平均查找、插入和删除 `O(1)`、最坏 `O(n)`；rehash `O(n)`，空间 `O(capacity)`。
- filter/map/take/skip/takeWhile/dropWhile：时间 `O(n)`，除 callable transient value 外额外空间 `O(1)`。
- stable sorted：时间 `O(n log n)`，额外空间 `O(n)`，受 `max_items` 限制。
- distinct：保持首次出现顺序，平均时间 `O(n)`，空间 `O(n)`，受 `max_unique` 限制。
- groupingBy/partitioningBy：平均时间 `O(n)`，空间由 bucket、entry 和 retained payload 三个上限共同约束。
- min/max/count/match/find：时间 `O(n)`；find/match 允许短路。

TurboSTL 提供稳定排序和 hash set 实现；CFlow 与 STLCFlow 不另写动态数组、hash table 或排序基础设施。以上复杂度是算法契约，不代表吞吐或缓存收益；性能结论必须来自实现后的 benchmark/profile。

## 错误契约

TurboSTL 定义：

```text
TURBO_STL_OK
TURBO_STL_INVALID_ARGUMENT
TURBO_STL_OUT_OF_MEMORY
TURBO_STL_CAPACITY_EXCEEDED
TURBO_STL_EMPTY
TURBO_STL_NOT_FOUND
TURBO_STL_TYPE_MISMATCH
TURBO_STL_TRAIT_MISSING
```

CFlow 定义结构化 `cflow_status`，至少区分：

```text
OK / EMPTY / INVALID_ARGUMENT / TYPE_MISMATCH
CAPACITY_EXCEEDED / OUT_OF_MEMORY / UNSUPPORTED / EXECUTION_ERROR
```

Graph、Stream 和 Run 保留第一个有效错误：

```c
typedef struct cflow_error {
    cflow_status status;
    cflow_stage stage;
    cflow_operator op;
    const char *message;
} cflow_error;
```

fluent intermediate method 继续返回 self，失败后 stream 进入 sticky failed 状态。terminal 返回 `cflow_status`；详细信息从 stream/run error accessor 读取。STLCFlow 在唯一适配边界把 `turbo_stl_status` 转换为 `cflow_status`。中间层不重复记录日志，应用在消费或转换错误的边界决定是否记录。

空流语义：

- count 返回 `OK, 0`；
- findFirst/findAny/min/max 和无 identity reduce 返回 `EMPTY`；
- anyMatch 返回 `OK, false`；
- allMatch/noneMatch 返回 `OK, true`；
- toArray/collect/groupingBy/partitioningBy 返回 `OK` 和合法空结果。

非法参数、缺 traits、容量溢出、OOM 或 callback/generator 失败不得伪装成 `EMPTY`。

## 迁移顺序

1. 扩展 CMeta type/container traits，并建立 traits、multi-TU 与 ownership tests。
2. 建立 `TurboUtils::STL`，合并 Core 与旧 `turbo/` 的标准容器实现。
3. 迁移 Core automata、TurboSerial 等调用方和构建依赖。
4. 扩展 CFlow operator/terminal/status/backend 协议，保持无 TurboSTL 的核心测试可运行。
5. 建立 `TurboUtils::STLCFlow`，实现 backend、stream adapter 和 typed collectors。
6. 把旧 Stream 中有限容器流的有效行为测试迁移到 CFlow/STLCFlow。
7. 删除旧 `stream/`、旧 `turbo/` 与 Utils 中全部标准容器文件和失效引用。
8. 更新安装导出、文档、examples、benchmark 与 install consumer smoke test。

迁移过程中每个阶段都必须保持可构建；不暴露部分实现 API。若一个 terminal 尚未完整实现，则它不进入 public schema/header。

## 测试矩阵

TinyTest 测试按模块拆分：

```text
cmeta_traits_test
turbostl_algorithms_test
turbostl_list_test
turbostl_ordered_test
turbostl_hash_test
turbostl_ownership_test
turbostl_multi_tu_test
cflow_terminals_test
turbostl_cflow_test
core_container_consumers_test
install_consumer_smoke
```

必须覆盖：

- 0、1、恰好上限、上限加一、最大合法值和 `SIZE_MAX` 乘加溢出；
- built-in traits、自定义 owning traits、缺 trait、type mismatch；
- copy/move/destroy 次数、每个失败注入点、collector commit/abort；
- raw 与 typed 容器的全部算法行为以及 stable sort；
- List 节点稳定性、双向 iterator、头尾/中间删除，以及旧 reserve/capacity/at 不再公开；
- Map/Set/MultiMap 的全部 insertion/deletion rotation 情形、随机操作后的红黑不变量、中序顺序、bounds 和重复 key 顺序；
- HashMap/HashSet 的极端 collision、tombstone 复用、70% load boundary、rehash 和未指定遍历顺序；
- default/key/value/entry Range、generation mismatch 和 source lifetime；
- Java 空流语义、短路、顺序、稳定 distinct/sort、重复执行；
- grouping/partition conflict policy、bucket/entry/payload 三重容量；
- peek effect 不被优化器移动，Surface/normalized/optimized parity；
- unsupported plan/backend 明确失败且无 fallback；
- 两个 C translation unit 与一个 C++17 TU 的 descriptor 语义一致性；
- 安装树只包含新 headers/targets，新 consumer 通过 `find_package(TurboUtils)` 构建。

## 构建与验证

Windows 首选仓库实际可用的 user preset：

1. `cmake --fresh --preset win-release-user`；
2. 先构建并运行 CMeta、TurboSTL、CFlow、STLCFlow 最小相关 targets；
3. 再运行 Core automata、regex、TurboSerial 相邻回归；
4. 扩大到 `ctest --preset win-release-user`；
5. 使用 `win-dev-user` 做 ASan/开发配置验证；
6. 使用实际可用的 Clang/GCC preset 验证严格 C11 和 public C++17 headers；
7. 安装到临时 prefix，并编译独立 `find_package` consumer。

测试必须通过 preset 和 `cmake_add_test()` 接入。TinyTest 提供 main；每个 `it(...)` 验证一个行为，优先使用 typed assertions。benchmark 使用 `benchmark_ops`/`benchmark_bytes` 等明确单位接口，setup 位于计时块外，并以独立断言验证结果。

## 兼容性风险与验证

### HIGH：公开 API、header 与 target 破坏

事实：旧 include、typed 宏和 `TurboUtils::Stream` 被删除；List 不再提供 deque 容量/随机访问接口，Map 不再接受 hash/equal 或暴露 slot/capacity。影响所有直接消费者。验证安装 manifest、CMake export、compile-negative fixtures 和独立 consumer；在 release notes 给出一对一迁移表。本设计明确不提供兼容层。

### HIGH：拥有型值生命周期错误

推论：copy/move/destroy 接入所有容器和 CFlow transient value 后，遗漏或重复调用会导致泄漏、double-free 或 use-after-free。使用计数型 owning value、故障注入、ASan 和泄漏检查验证每条 cleanup 路径。

### HIGH：Range 执行期 mutation

事实：Range cursor 从 ordinal `size_t` 迁移为 opaque cursor，CFlow source 是当前直接调用方；任何 container mutation 都使 Range 执行失效。同步迁移 CMeta generated adapters 与 `cflow/src/sources.c`，使用 mutation generation fail fast，并测试 callback 中 mutation。直接裸指针写入仍作为明确契约限制记录。

### MED：优化与状态算子语义偏移

推论：新增 state/effect operator 可能被错误重排或编译。以 observable parity、effect barrier 和 unsupported plan tests 验证；优化统计不作为行为依据。

### MED：Core 调用方迁移遗漏

事实：automata 与 TurboSerial 当前直接 include/use Vec。使用 `rg.exe` 检查旧路径和旧 target 零引用，并运行相邻测试及安装 consumer。

## 状态归属与失败后状态

- source container 是输入事实源，由调用方拥有；CFlow 不修改它。
- graph 是 pipeline intent 的唯一事实源，由 `cflow_stream` 拥有。
- normalize/optimized graph 和 plan 是从 graph 派生的独立对象，失败时销毁，不回写 source graph。
- terminal run state 只属于一次 evaluation；成功或失败后全部关闭。
- collector 未 commit 前拥有临时输出；commit 后所有权转给调用方，abort 后输出为零状态。
- 没有外部不可回滚副作用，唯一例外是 effectful user callback；该 effect 不重试、不回滚，并通过 effect metadata 阻止重排。callback 失败后停止执行并报告第一个错误。

## 回滚方案

本改动不迁移持久化数据。合入或发布前若验证失败，按阶段回退 TurboSTL/CMeta/CFlow 相关提交并恢复上一版本构建图，不在失败实现上增加 fallback 分支。发布后需要旧接口的用户继续使用上一 major/minor package；新版本不动态切换回旧 Stream runtime，也不同时维护两套容器实现。

## 完成条件

- 标准容器文件只存在于 `turbostl/`，Core 和旧 `turbo/` 无重复实现；
- CFlow target 不依赖 TurboSTL，STLCFlow 是唯一适配实现；
- 公开 typed API 只有 CMeta `typed/Containers`；
- 有限容器 Java 风格操作面按本设计完整公开，没有占位 terminal；
- ownership、capacity、mutation、status 和 collector 状态机测试通过；
- MSVC、Clang/GCC、C++ public-header、全量 CTest 和 install consumer 验证通过；
- 文档、examples、安装树和 CMake exports 与新架构一致。
