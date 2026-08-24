# TurboUtils Canonical Architecture

日期：2026-08-24  
状态：Canonical repository architecture

本文定义 TurboUtils 当前仓库级模块边界、public target ownership、依赖方向，以及与
TurboParser 的集成边界。专项 API、错误语义、ABI 和阶段能力仍以公开头文件、测试及
`docs/superpowers/specs/` 中的专项设计为事实源；本文不重复低层契约。

> 图中实线表示当前 TurboUtils CMake target 依赖；虚线表示已经接受的跨仓库集成方向，
> 不表示对应 TurboParser migration 已经落地。

## 1. Canonical module map

```mermaid
flowchart TB
    subgraph TU[TurboUtils]
        subgraph Foundation[Semantic / protocol foundation]
            CMeta[TurboUtils::CMeta<br/>type identity / traits / reflection<br/>semantic data / container contracts]
            CSerde[TurboUtils::CSerde<br/>canonical token reader / writer protocol]
            CBind[TurboUtils::CBind<br/>native C data ↔ canonical values]
        end

        subgraph Execution[Execution]
            CFlow[TurboUtils::CFlow<br/>typed graph / stream / machine<br/>lower / analyze / optimize / execute]
            Platform[TurboUtils::Platform]
            Concurrency[TurboUtils::Concurrency]
        end

        subgraph Containers[Containers]
            STL[TurboUtils::STL<br/>canonical containers + CMeta projection]
            STLStream[TurboUtils::STLStream<br/>STL + CFlow composition]
        end

        Core[TurboUtils::Core<br/>general utilities]

        CBind --> CMeta
        CBind --> CSerde

        Concurrency --> Platform
        CFlow --> CMeta
        CFlow -. private .-> Platform
        CFlow -. private .-> Concurrency

        STL --> CMeta
        STLStream --> STL
        STLStream --> CFlow

        Core --> CMeta
        Core --> Platform
        Core --> Concurrency
        Core -. private .-> STL
        Core -. private .-> CFlow
    end

    subgraph TP[TurboParser]
        Parsers[parser/*<br/>JSON / YAML / XML / CSV / ...]
        DataBind[TurboParser::DataBind]
        TBE[TBE schema / wire]
        FlowAdapters[Parser + CBind + CFlow composition]
    end

    Parsers -. format projection .-> CSerde
    DataBind -. generic native binding .-> CBind
    TBE -. semantic bridge .-> CMeta
    FlowAdapters -. bind complete values .-> CBind
    FlowAdapters -. Stream&lt;T&gt; execution .-> CFlow
```

依赖箭头统一解释为“箭头起点依赖箭头终点”。TurboUtils 不反向依赖 TurboParser。

`tinytest/`、vendor、build tools 与 Lean/formal generation 属于测试、构建或验证平面，
不进入上面的 runtime ownership 图。`turbo_serial` 是 serial-port/串口子系统，与
CSerde/CBind serialization architecture 没有语义 ownership 关系。

## 2. Single sources of truth

### CMeta — type and semantic truth

`TurboUtils::CMeta` 是整个体系最底层的类型与语义事实源，负责：

- type identity、type traits、Enum/Struct reflection；
- callable / relation / interface / contract 元数据；
- semantic data descriptors；
- generic/declared type 与 container descriptor；
- Range、Collector 等跨容器协议。

CMeta 不实现容器算法、不解析具体数据格式，也不拥有 CFlow runtime。

### CSerde — canonical data-event truth

`TurboUtils::CSerde` 定义 format-neutral canonical token、reader/writer contract 和 view
lifetime 语义。它当前没有 TurboUtils public target 依赖，也不拥有 JSON/YAML/XML/CSV
parser。

具体格式由 parser/codec adapter 将 native syntax/events 投影为 CSerde，而不是在 CSerde
内部建立第二套 parser。

### CBind — native binding truth

`TurboUtils::CBind` 只依赖 `TurboUtils::CMeta + TurboUtils::CSerde`。它负责依据 CMeta
semantic shape 在 canonical CSerde values 与 native C storage 之间绑定。

因此 CBind 是 parser-independent kernel：数据库、IPC、自定义 binary source 或测试
provider 只要实现 CSerde contract，也可以直接复用 CBind。CBind 不直接依赖
TurboParser、TurboSTL、CFlow 或 Core。

### CFlow — execution truth

`TurboUtils::CFlow` 是 typed structured graph/dataflow compiler and runtime。其 public
依赖只有 CMeta；Platform 与 Concurrency 是 private execution substrate。

CFlow 不拥有容器算法、不解析 serialization format，也不把 raw `cserde_token` 当作可
任意 `filter/map` 的业务 `Stream<T>`。Parser/CBind/CFlow 的组合边界位于完整
semantic/native value 上。

### TurboSTL — container truth

`TurboUtils::STL` 是标准容器算法和实例 metadata 的事实源，并通过 CMeta container
contract 暴露类型、Range、Collector 与 construction 能力。

CFlow 本身不依赖 TurboSTL。`TurboUtils::STLStream` 是显式 INTERFACE composition target，
只把 `TurboUtils::STL + TurboUtils::CFlow` 组合给需要 container stream API 的使用者。

### Platform / Concurrency — execution substrate

`TurboUtils::Platform` 提供最底层 platform abstraction。`TurboUtils::Concurrency` 在其上
提供 thread pool、Disruptor 等并发基础能力，并公开依赖 Platform。

CFlow 私有消费 Platform/Concurrency，因此这些 runtime implementation dependencies 不会
改变 CFlow 的 public semantic dependency：`CFlow -> CMeta`。

### Core — general utility layer

`TurboUtils::Core` 保留字符串、文件、日志、正则、进程、内存及其他通用工具。它公开依赖
CMeta、Platform、Concurrency，并私有消费 STL/CFlow；Core 不应重新成为 container、
metadata、data binding 或 execution semantics 的第二事实源。

## 3. CFlow internal architecture

CFlow 当前主链路为：

```mermaid
flowchart LR
    Surface[Surface API<br/>Stream / Graph / Operators]
    Lower[Lower]
    Normalized[Normalized IR]
    Analysis[Effect / Property / Verify]
    Optimize[Optimize]
    Plan[Primitive IR / Execution Plan]
    Direct[Direct executor]
    Runtime[Runtime / Scheduler / Reactive]
    Machine[Typed Machine IR]
    MachineRuntime[Machine Runtime]

    Surface --> Lower --> Normalized
    Normalized --> Analysis --> Optimize --> Plan
    Plan --> Direct
    Plan --> Runtime
    Normalized --> Machine --> MachineRuntime --> Runtime
```

等价职责流：

```text
Surface Graph / Stream / Operators
        ↓ lower
Normalized semantic IR
        ↓ analyze / verify / optimize
Primitive IR / compiled execution plan
        ├── direct execution
        └── runtime / scheduler / reactive execution

Typed Machine definition
        ↓ transactional normalize / validate
Immutable Machine IR
        ↓ instance / Event mailbox / executor
Machine Runtime
        ↓ source adapter / demand
CFlow Run
```

CMeta 为这些层提供 type/callable/relation/interface semantics；CFlow 负责 execution
semantics。两者不复制彼此的事实源。

## 4. Data and parser integration

Canonical data path 固定为：

```text
native format syntax
        ↓
TurboParser parser/event model
        ↓ format projection
CSerde canonical values
        ↓
CBind + CMeta semantic shape
        ↓
native C value
        ↓ optional composition
CFlow Stream<T> / Graph / Machine
```

反方向写出遵守同一分层：native value 由 CBind/CMeta 映射到 canonical writer contract，
具体 serializer 再把 canonical events 转成目标 syntax/wire。

关键禁止项：

- `CBind -> TurboParser`；
- `CBind -> TurboSTL`；
- `CBind -> CFlow`；
- `CFlow -> TurboSTL`；
- `CSerde -> concrete parser`；
- 把 `Stream<cserde_token>` 暴露为可任意 `filter/map` 的业务 stream；
- 在 DataBind/TBE/CFlow 中维护第二套通用 type/semantic/binding truth。

TurboParser 当前是独立 package，其现有公共 parser runtime 仍以 `TurboUtils::Core` 为基础
依赖。Parser -> CSerde、DataBind -> CBind、Parser + CBind + CFlow composition 是已经接受的
迁移/集成方向；在对应 TurboParser PR 落地之前，这些关系必须继续表示为虚线，而不能写成
当前 target dependency。

## 5. Public target dependency matrix

| Target | Public dependencies | Private / composition dependencies | Canonical ownership |
| --- | --- | --- | --- |
| `TurboUtils::CMeta` | none | none | type / semantic metadata |
| `TurboUtils::CSerde` | none | none | canonical token protocol |
| `TurboUtils::CBind` | `CMeta`, `CSerde` | none | native data binding |
| `TurboUtils::Platform` | none | platform libraries | platform abstraction |
| `TurboUtils::Concurrency` | `Platform` | none | concurrency substrate |
| `TurboUtils::CFlow` | `CMeta` | `Platform`, `Concurrency` | graph/dataflow execution |
| `TurboUtils::STL` | `CMeta` | none | container algorithms |
| `TurboUtils::STLStream` | `STL`, `CFlow` | INTERFACE composition | container stream integration |
| `TurboUtils::Core` | `CMeta`, `Platform`, `Concurrency` | `STL`, `CFlow` plus utility vendors | general utilities |

该表只描述 canonical TurboUtils public/runtime target graph；具体第三方 vendor 与 build/test
target 不属于此表的 ownership 语义。

## 6. Architectural invariants

1. **依赖只向基础事实源收敛。** CMeta/CSerde 不因上层使用场景反向依赖 CBind、CFlow、
   TurboSTL 或 TurboParser。
2. **同一语义只保留一个 truth。** 类型与 semantic shape 属于 CMeta；canonical events
   属于 CSerde；native binding 属于 CBind；execution 属于 CFlow；container algorithms
   属于 TurboSTL。
3. **repo ownership 与 link dependency 分离。** TurboParser 可以消费 CBind/CSerde，但不会
   因此改变它们属于 TurboUtils 的事实。
4. **组合能力位于 adapter/composition layer。** Parser + CBind、CBind + CFlow、STL + CFlow
   不通过反向依赖污染底层 kernel。
5. **raw structural transport 不是业务 stream。** CSerde token grammar 必须完整保留；CFlow
   pipeline 从完整 semantic/native value 边界开始。
6. **PUBLIC 与 PRIVATE dependency 不混淆。** execution implementation 可以私有依赖
   Platform/Concurrency，但不能把无关 substrate 泄漏为 CFlow public semantic contract。
7. **canonical 边界不得静默漂移。** 如果 public target ownership 或依赖方向改变，先更新
   本文，并在对应专项 spec 中明确 migration 与验证方式。

## 7. Detailed design references

以下文档保留各专项的详细 reasoning。若历史描述与当前公开 target/CMake/test 不一致，
以当前公开接口、测试和本文的 repo-level ownership 为准：

- `cflow/README.md` — CFlow public surface、lower/IR/runtime/Machine；
- `docs/superpowers/specs/2026-08-21-container-cmeta-cflow-design.md` — TurboSTL/CMeta/CFlow 分层来源；
- `docs/superpowers/specs/2026-08-22-cmeta-cflow-calculus-v1-design.md` — CMeta/CFlow calculus 与 operator policy；
- `docs/superpowers/specs/2026-08-22-cflow-execution-foundation-design.md` — execution substrate；
- `docs/superpowers/specs/2026-08-23-serialization-data-binding-design.md` — CMeta/CSerde/CBind/TurboParser data architecture；
- `docs/superpowers/specs/2026-08-24-cflow-machine-runtime-design.md` — typed Machine runtime。
