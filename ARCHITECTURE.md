# TurboUtils Canonical Architecture

日期：2026-08-24  
状态：Canonical repository architecture

本文定义 TurboUtils 当前仓库级模块边界、public target ownership、依赖方向，以及与
TurboParser 的集成边界。专项 API、错误语义、ABI 和阶段能力仍以公开头文件、测试及
`docs/superpowers/specs/` 中的专项设计为事实源；本文不重复低层契约。

> 主图采用客户文稿视角，表达产品、语义 IR、CMeta 与 Platform/OS 的分层关系；右侧
> `Cross-Cutting Capabilities` 表达跨层能力归属，不等价于 CMake target 的层级包含关系。
> 实际 public/private target 依赖仍以第 5 节 dependency matrix 为准。

## 1. Layered canonical architecture

```mermaid
flowchart TB

    subgraph BODY[" "]
        direction LR

        subgraph STACK[" "]
            direction TB

            subgraph L4["Layer 4 — User Models (Products / APIs)<br/>面向应用开发者"]
                direction LR
                STREAM["<b>Stream</b><br/>Java-aligned Stream API<br/>stream(&amp;users, &amp;s) → filter → map → to_list"]
                ACTOR["<b>Actor</b><br/>Actor Model · future"]
                STATECHART["<b>Statechart</b><br/>State Machine · future"]
                WORKFLOW["<b>Workflow</b><br/>Workflow / Pipeline · future"]
                REACTIVE["<b>IO / Reactive</b><br/>IO Pipeline / Reactive Style · future"]
                MORE["<b>...</b><br/>More Abstractions"]
            end

            subgraph L3["Layer 3 — CFlow Kernel (Execution Runtime)<br/>可独立使用的运行时产品"]
                direction TB

                subgraph KERNEL["Execution Kernel — Portable Semantic Execution Runtime"]
                    direction LR
                    RUN["<b>Run / Task</b><br/>run / resume<br/>state machine<br/>demand / backpressure<br/>cancel / error"]
                    SOURCE["<b>Source / Sink</b><br/>Source: VALUE / WAIT / DONE / ERROR<br/>Sink: value / error / done"]
                    WAIT["<b>Waitable / Waker</b><br/>WAIT → Waitable<br/>Waker.wake()<br/>OS event integration"]
                    EXEC["<b>Executor</b><br/>post(task)<br/>thread pool<br/>coroutine executor<br/>serial / manual"]
                    CLOCK["<b>Clock / Timer</b><br/>Clock<br/>Timer<br/>VirtualClock"]
                end

                subgraph KSVC["Kernel Services"]
                    direction LR
                    SCHED["Scheduler"]
                    TQ["TimerQueue"]
                    CANCEL["Cancellation"]
                    DEMAND["Demand / Backpressure"]
                    ERROR["Error Propagation"]
                    KTRACE["Metrics / Tracing"]
                end
            end

            subgraph L2["Layer 2 — CFlow Graph IR (Semantic Framework)<br/>框架内部语义模型与执行合同"]
                direction LR
                GRAPH["<b>Graph Model</b><br/>Graph / Subgraph<br/>Node / Edge / Relation"]
                OPS["<b>Operators (Relational)</b><br/>source · map · filter · flat_map · relation<br/>zip · merge · group_by · window · ..."]
                SEM["<b>Semantic Info</b><br/>Type / Trait — from CMeta<br/>Cardinality<br/>Effects / Purity<br/>Properties"]
                PROCESS["<b>Graph Processing</b><br/>Validation<br/>Optimization<br/>Lowering → Execution Plan"]
            end

            subgraph L1["Layer 1 — CMeta (Semantic Toolkit)<br/>语义工具 / DSL / 代码生成基础设施"]
                direction LR
                TYPE["<b>Type System</b><br/>type / struct / union / enum<br/>alias / generic<br/>const / volatile / ..."]
                TRAIT["<b>Trait & Property</b><br/>trivial / copyable<br/>comparable / hashable<br/>range / sized / sorted / ..."]
                INTERFACE["<b>Interface</b><br/>interface(...)<br/>vtable / impl<br/>capability / contract"]
                CALLABLE["<b>Callable</b><br/>function signature<br/>pure / side-effect<br/>noexcept / async / ..."]
                REFLECT["<b>Reflection & Schema</b><br/>type descriptor<br/>struct offset / size<br/>schema / reflection"]
                CODEGEN["<b>Codegen & Tools</b><br/>header / impl generation<br/>mock / stub generation<br/>tooling"]
            end

            subgraph PLATFORM["Platform / OS — Platform Abstraction (OS Primitives)<br/>基础设施"]
                direction LR
                THREAD["<b>Threading</b><br/>thread<br/>mutex / rwlock<br/>condvar"]
                EVENT["<b>Event / Waitable</b><br/>binary event<br/>wait / signal<br/>reset / wait_for"]
                POLLER["<b>Poller</b><br/>epoll / kqueue<br/>IOCP / poll<br/>native handle"]
                TIMER["<b>Timer</b><br/>sleep / timerfd<br/>CreateTimerQueue<br/>dispatch source"]
                IO["<b>I/O</b><br/>socket / pipe<br/>file / device<br/>async I/O"]
            end
        end

        subgraph CROSS["Cross-Cutting Capabilities"]
            direction TB
            LOG["<b>Logging & Tracing</b>"]
            METRICS["<b>Metrics & Monitoring</b>"]
            CONFIG["<b>Configuration</b>"]
            CSERDE["<b>CSerde</b><br/>Canonical Token Protocol"]
            CBIND["<b>CBind</b><br/>Native Data Binding"]
            TESTING["<b>Testing & Simulation</b>"]
        end
    end

    STREAM -->|"Build on"| RUN
    ACTOR -.-> RUN
    STATECHART -.-> RUN
    WORKFLOW -.-> RUN
    REACTIVE -.-> SOURCE

    RUN -->|"Execute"| GRAPH
    SOURCE --> OPS
    WAIT --> SEM
    EXEC --> PROCESS

    GRAPH -->|"Define & Describe"| TYPE
    OPS --> CALLABLE
    SEM --> TRAIT
    PROCESS --> REFLECT

    TYPE -. "Describe Types & Contracts" .-> THREAD
    INTERFACE -.-> EVENT
    CALLABLE -.-> POLLER
    REFLECT -.-> TIMER

    RUN --- SCHED
    SOURCE --- TQ
    WAIT --- CANCEL
    EXEC --- DEMAND
    CLOCK --- ERROR
    ERROR --- KTRACE

    %% Cross-cutting ownership does not change target dependencies.
    CSERDE -. "canonical token transport" .-> SOURCE
    CBIND -->|"semantic types / reflection"| REFLECT
    CBIND -->|"depends on"| CSERDE
    CBIND -. "native values" .-> STREAM

    LOG -.-> RUN
    METRICS -.-> KTRACE
    CONFIG -.-> RUN
    TESTING -.-> GRAPH

    classDef layer4 fill:#fbf8ff,stroke:#a78bfa,color:#3b1c88,stroke-width:1px;
    classDef kernel fill:#f5faff,stroke:#60a5fa,color:#174ea6,stroke-width:1px;
    classDef graph fill:#f7fbf4,stroke:#86b874,color:#176126,stroke-width:1px;
    classDef meta fill:#fff9f1,stroke:#f6a34a,color:#c74f00,stroke-width:1px;
    classDef platformStyle fill:#f7f9fc,stroke:#8296b3,color:#183a64,stroke-width:1px;
    classDef cross fill:#f4fbf8,stroke:#72b8a3,color:#075d4c,stroke-width:1px;
    classDef binding fill:#effaf5,stroke:#41a27c,color:#075d4c,stroke-width:1.5px;

    class STREAM,ACTOR,STATECHART,WORKFLOW,REACTIVE,MORE layer4;
    class RUN,SOURCE,WAIT,EXEC,CLOCK,SCHED,TQ,CANCEL,DEMAND,ERROR,KTRACE kernel;
    class GRAPH,OPS,SEM,PROCESS graph;
    class TYPE,TRAIT,INTERFACE,CALLABLE,REFLECT,CODEGEN meta;
    class THREAD,EVENT,POLLER,TIMER,IO platformStyle;
    class LOG,METRICS,CONFIG,TESTING cross;
    class CSERDE,CBIND binding;

    style L4 fill:#fcfaff,stroke:#b7a2ef,stroke-width:1px,color:#38137a
    style L3 fill:#f4f9ff,stroke:#72aef5,stroke-width:1px,color:#124694
    style L2 fill:#f7fbf4,stroke:#91bb7b,stroke-width:1px,color:#176126
    style L1 fill:#fff8ef,stroke:#f1a452,stroke-width:1px,color:#c44e00
    style PLATFORM fill:#f6f8fb,stroke:#8798b0,stroke-width:1px,color:#183a64
    style CROSS fill:#f4fbf8,stroke:#72b8a3,stroke-width:1px,color:#075d4c
    style BODY fill:transparent,stroke:transparent
    style STACK fill:transparent,stroke:transparent
```

这张图描述的是**分层产品模型**：Stream 是当前首要用户产品；Actor、Statechart、Workflow、
IO/Reactive 是可在同一 Kernel 上构建的后续 façade/model，不表示当前已经存在独立 public
runtime target。Layer 2/3 共同属于 CFlow；Layer 1 是 CMeta 语义工具层；Platform/OS 提供
底层执行原语。

`CSerde` 与 `CBind` 在客户视角中属于 `Cross-Cutting Capabilities`：它们横跨 parser、native
value、Stream/Graph 等使用场景，但这不改变模块依赖事实。当前 target 仍然是
`CBind -> CMeta + CSerde`，`CSerde` 不依赖 CFlow/CMeta，CBind 也不依赖 CFlow、TurboSTL、
Core 或 TurboParser。

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

`TurboUtils::Platform` 提供最底层 platform abstraction，并公开依赖 CMake 的
`Threads::Threads`。`TurboUtils::Concurrency` 在其上提供 thread pool、Disruptor 等并发
基础能力，并公开依赖 Platform。

CFlow 私有消费 Platform/Concurrency，因此这些 runtime implementation dependencies 不会
改变 CFlow 的 public semantic dependency：`CFlow -> CMeta`。

### Core — general utility layer

`TurboUtils::Core` 保留字符串、文件、日志、正则、进程、内存及其他通用工具。它公开依赖
CMeta、Platform、Concurrency，并私有消费 STL/CFlow；Core 不应重新成为 container、
metadata、data binding 或 execution semantics 的第二事实源。

## 3. CFlow internal architecture

CFlow 当前存在两个互补入口：dataflow graph/stream pipeline 与 typed Machine pipeline。
两者共享 CMeta semantics 和 CFlow runtime，但 Machine 不是从 Normalized Graph 派生的
第二种表示。

```mermaid
flowchart LR
    Surface[Surface API<br/>Stream / Graph / Operators]
    Lower[Lower]
    Normalized[Normalized IR]
    Analysis[Effect / Property / Verify]
    Optimize[Optimize]
    Plan[Primitive IR / Execution Plan]
    Direct[Direct executor]
    Runtime[CFlow Run<br/>Runtime / Scheduler / Reactive]

    MachineDef[Typed Machine Definition]
    MachineBuild[Validate / Normalize]
    Machine[Immutable Machine IR]
    MachineRuntime[Machine Runtime]

    Surface --> Lower --> Normalized
    Normalized --> Analysis --> Optimize --> Plan
    Plan --> Direct
    Plan --> Runtime

    MachineDef --> MachineBuild --> Machine --> MachineRuntime
    MachineRuntime -. source adapter / demand .-> Runtime
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
        ↓ optional Source adapter / Run demand
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
| `TurboUtils::Platform` | `Threads::Threads` | platform implementation | platform abstraction |
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
