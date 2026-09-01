# 第四章：从 Callable 到 Graph——把计算本身变成数据

上一章完成了一个非常重要的变化。

最开始，我们处理的是：

```text
Typed Data
```

后来进一步得到：

```text
Typed Callable
```

于是一个函数不再只是：

```text
function pointer
```

而可以同时具有：

```text
Signature
Effects
Properties
Capture
Dispatch
```

甚至可以通过：

```text
Lambda
Bind
Generator
```

构造新的 Callable。

这时，一个新的问题自然出现：

> **如果一个函数已经可以成为一个有类型、有语义的数据对象，那么多个函数之间的计算关系，能不能也成为数据？**

例如：

```text
User
 ↓
enabled : User -> bool
 ↓
name : User -> String
```

代码当然可以直接写成：

```c
if (enabled(user)) {
    String result = name(user);
}
```

但一旦直接执行以后：

```text
enabled
name
```

之间的关系就只存在于：

```text
控制流
```

里面。

如果希望在执行之前：

```text
检查它
分析它
修改它
优化它
选择执行方式
```

那么就需要先把：

> **计算本身保存下来。**

这就是 Graph 出现的原因。

---

## 1. Graph 首先不是为了“画图”

提到 Graph，很容易想到：

```text
Node
Edge
```

或者可视化流程图。

但这里 Graph 真正的意义不是为了画图。

它解决的是一个更重要的问题：

> **把一段原本只能通过执行表达的计算，变成一个普通的数据结构。**

例如原来的程序：

```c
x = f(input);
y = g(x);
result = h(y);
```

可以先表示成：

```text
Input
  ↓
f
  ↓
g
  ↓
h
  ↓
Output
```

一旦这个关系被保存下来，程序就第一次拥有了两个阶段：

```text
Describe
   ↓
Execute
```

而不是：

```text
Describe = Execute
```

这一步非常关键。

因为只有当：

```text
Program
```

首先成为：

```text
Data
```

以后，系统才能在执行之前对它做进一步处理。

---

## 2. 一个 Node 不应该只是 callback

最简单的 Graph Node 可以写成：

```c
struct Node {
    function_pointer fn;
};
```

但如果这样做，前面建立的类型和语义信息又全部丢失了。

更合理的 Node 应该知道：

```text
Operator
Input Type
Output Type
Callable
Effects
Properties
```

例如：

```text
Map Node

Input Type  = User
Output Type = String
Callable    = user_name
Effects     = PURE
Properties  = DETERMINISTIC | TOTAL
```

或者：

```text
Filter Node

Input Type  = User
Output Type = User
Predicate   = enabled : User -> bool
```

于是 Node 不只是：

> 调一个函数。

而是：

> **一个具有明确输入、输出和语义的计算单元。**

---

## 3. Edge 也开始具有类型意义

普通 Graph 中：

```text
A -> B
```

只表示连接关系。

Typed Graph 中，这条 Edge 还意味着：

```text
A.output_type
    ==
B.input_type
```

例如：

```text
A
int -> double

B
double -> long
```

可以连接：

```mermaid
flowchart LR
    A["A<br/>int → double"]
    B["B<br/>double → long"]

    A -->|"double"| B
```

但：

```text
A
int -> double

B
String -> User
```

显然不能直接连接。

因此 Graph 本身已经包含一个：

```text
Type Constraint System
```

而不是等真正运行到 B 时才发现：

```text
传进来的数据不对
```

---

## 4. 这使错误可以第一次提前到“构造阶段”

传统 callback pipeline 很容易出现：

```text
Callback A
    ↓
void *
    ↓
Callback B
```

只要双方都接受：

```text
void *
```

编译器就很难帮助检查真实 payload。

问题往往到：

```text
runtime
```

才出现。

Typed Graph 可以在：

```text
Graph Build
```

阶段就检查：

```text
Node A output type
Node B input type
```

如果不兼容：

```text
Graph 不成立
```

根本不进入执行。

这形成了一个以后一直非常重要的原则：

> **能在构造阶段拒绝的问题，就不要留给执行阶段。**

---

## 5. Graph 是对前面所有 Meta 能力的一次真正组合

到这里，前面几个章节的能力第一次同时进入一个复杂对象。

```mermaid
flowchart TD
    T["Type"]
    TR["Traits"]
    C["Callable"]
    E["Effects"]
    P["Properties"]
    I["Identity"]

    N["Graph Node"]

    T --> N
    TR --> N
    C --> N
    E --> N
    P --> N
    I --> N
```

其中：

```text
Type
```

告诉 Graph：

```text
数据是什么
```

```text
Callable
```

告诉 Graph：

```text
要执行什么
```

```text
Effects / Properties
```

告诉 Graph：

```text
这个操作具有什么语义
```

```text
Type Identity
```

则保证这些信息在：

```text
多个 Translation Unit
Library
Generated Metadata
```

之间仍然可以正确比较。

这也是为什么 Graph 是验证前面整个设计是否真正可组合的一个非常好的对象。

---

## 6. Graph 首先解决的，是复杂的数据转换

一旦有：

```text
Typed Node
+
Typed Edge
```

以后，最自然的用途就是：

```text
Data Transformation
```

例如：

```text
User
 ↓
检查 enabled
 ↓
User
 ↓
取得 name
 ↓
String
 ↓
转换长度
 ↓
size_t
```

可以写成：

```mermaid
flowchart LR
    A["Input<br/>User"]
    B["Filter<br/>enabled"]
    C["Map<br/>User → String"]
    D["Map<br/>String → size_t"]

    A --> B --> C --> D
```

这里真正描述的是：

```text
数据怎样一步一步变化
```

而不是：

```text
循环变量怎么移动
临时变量叫什么
continue 放在哪里
```

这使业务代码第一次可以更多地关注：

```text
Transformation
```

而不是：

```text
Iteration Mechanics
```

---

## 7. 普通 C 循环本身没有问题

这一点需要特别说明。

例如：

```c
for (size_t i = 0; i < count; ++i) {
    if (!enabled(&users[i]))
        continue;

    names[out++] = user_name(&users[i]);
}
```

这段代码：

```text
简单
直接
高效
```

完全没有必要因为存在 Graph 就全部替换掉。

Graph 解决的是另一类问题。

当数据转换不断增加：

```text
Filter
Map
FlatMap
Limit
Skip
Reduce
Collect
```

并且同样的执行逻辑在多个地方重复时，开始出现：

```text
重复循环
重复中间 buffer
重复错误处理
重复类型转换
```

这时把：

```text
数据转换关系
```

和：

```text
执行机制
```

分开才开始真正有价值。

---

## 8. Graph 让“关系”比“过程”更重要

例如用户真正想表达：

```text
只保留 enabled User
        ↓
取得名字
        ↓
只取前 100 个
```

Graph 中可以直接保存：

```text
Filter(enabled)
Map(name)
Limit(100)
```

而不是保存：

```text
for
if
continue
counter++
break
```

可以理解成：

```text
Imperative C
    描述“怎么做”

Graph
    描述“要做什么”
```

当然最终执行时仍然必须变回：

```text
怎么做
```

但这个过程可以由后面的 Executor 或 Compiler 完成。

---

## 9. 一旦程序变成数据，就可以“看见整个程序”

这是 Graph 最重要的价值之一。

如果：

```c
x = f(input);
y = g(x);
z = h(y);
```

已经直接执行，系统很难在：

```text
h
```

运行时突然回头问：

```text
前面完整的计算是什么？
```

Graph 则从一开始就拥有：

```text
f
g
h
```

完整结构。

所以执行之前可以问：

```text
一共有几个 Node？

数据类型如何传播？

有没有无效连接？

有没有永远不会执行的 Subgraph？

两个 Map 是否可以合并？

中间结果是否真的需要存在？
```

这就产生了一个小型 compiler 的基础。

---

## 10. Graph 自然带来 Validate

第一阶段最直接的工作是：

```text
Validate
```

例如检查：

```text
所有 Node 是否有效？
所有 Edge 是否指向存在的 Node？
输入输出 Type 是否连续？
Callable Signature 是否匹配 Operator？
Subgraph 是否存在？
Terminal Node 后是否还有非法连接？
```

可以表示成：

```mermaid
flowchart LR
    A["Surface Graph"]

    B["Validate"]

    C["Valid Graph"]

    D["Reject"]

    A --> B
    B -->|valid| C
    B -->|invalid| D
```

这一步完全属于：

```text
Control Plane
```

执行数据之前完成。

---

## 11. Graph 还自然带来 Type Propagation

假设：

```text
Input
    output = int
```

接下来：

```text
Map
    int -> double
```

那么 Graph 后续 flow type 就变成：

```text
double
```

再接：

```text
Map
    double -> long
```

继续得到：

```text
long
```

于是：

```text
Type
```

不再只是每个 Node 自己的 metadata。

它开始沿着 Graph 流动。

```mermaid
flowchart LR
    A["Input"]
    B["Map"]
    C["Map"]

    A -->|"int"| B
    B -->|"double"| C
    C -->|"long"| D["Output"]
```

这就是：

```text
Type Propagation
```

也是以后自动 inference 的基础。

---

## 12. 某些 Operator 的输出类型甚至可以推导

例如：

```text
Filter<T>
```

predicate 的函数类型是：

```text
T -> bool
```

但 Filter 的流输出仍然是：

```text
T
```

所以：

```text
Node output
```

并不总是简单等于：

```text
Callable return type
```

类似：

```text
FlatMap
Reduce
Collect
```

也各有自己的类型规则。

这正好可以复用前面已经建立的：

```text
Finite Relation
Inference
```

也就是说：

```text
Operator
+
Callable Signature
+
Input Type
        ↓
Output Type
```

本身也可以成为有限推导。

---

## 13. 这让 Graph 开始成为“类型化 IR”

到这里，Graph 已经不再只是：

```text
runtime callback graph
```

更接近：

```text
Intermediate Representation
```

因为它保存的不是具体机器执行步骤，而是：

```text
计算语义
类型关系
结构关系
```

例如一个高层 Node：

```text
Filter
```

并不规定：

```text
一定要使用 if
一定要单独调用一次 callback
一定要有一个 runtime node object
```

它只是表达：

> **保留满足 predicate 的数据。**

至于最后怎么执行，是下一阶段的事。

---

## 14. 这使“高级语义”和“低层执行”第一次解耦

例如：

```text
Map
```

在 Graph 中只有一个语义：

```text
T -> U
```

但它最后可能：

```text
由解释器执行
```

也可能：

```text
编译进 Plan
```

甚至：

```text
直接生成普通 C loop
```

所以：

```text
Graph
```

回答：

> 计算是什么意思？

而：

```text
Executor / Backend
```

回答：

> 这次具体怎样执行？

这是后面 CFlow 最核心的分层之一。

---

## 15. 为什么不能直接让 Graph 自己执行

最简单的实现当然可以：

```c
for each node {
    switch (node->op) {
        case MAP:
            ...
        case FILTER:
            ...
    }
}
```

作为第一个工作版本，这完全合理。

但如果永远这么执行，那么每一个 value 都可能经历：

```text
读取 Node
判断 Operator
读取 Descriptor
检查 Signature
查找下一个 Edge
动态 Dispatch
```

很多工作其实在 Graph 构造完成以后已经不会再改变。

例如：

```text
Node 类型
拓扑
Callable Signature
下一个 Node
```

所以没有必要在：

```text
每一个 value
```

上重新计算。

这开始引出：

> **Control Plane 和 Execution Plane 应该分开。**

---

## 16. Graph 构造阶段可以做复杂事情

Graph Build 可以承担：

```text
Type Validation
Signature Validation
Topology Validation
Inference
Effect Analysis
Property Analysis
```

这些操作即使相对复杂，也只执行：

```text
一次
```

或者：

```text
少数几次
```

然后得到：

```text
Validated Graph
```

后面 hot path 就可以更简单。

可以概括成：

```text
Build once
Execute many
```

对于大量数据转换，这个 trade-off 非常合理。

---

## 17. 下一步自然出现 Normalize / Lower

Graph 最开始应该优先：

```text
容易表达
```

而不是：

```text
容易执行
```

例如用户接口可能提供：

```text
较高级的 Structured Operator
```

这些 operator 适合人理解，却不一定适合 runtime 直接处理。

因此可以增加：

```text
Lower
```

阶段。

```mermaid
flowchart LR
    A["Surface Graph"]

    B["Normalize / Lower"]

    C["Primitive Graph"]

    A --> B --> C
```

高层 API 负责：

```text
表达力
```

Primitive IR 负责：

```text
执行简单
```

这与传统 compiler：

```text
Source Language
 ↓
IR
```

是类似的思路。

---

## 18. Graph 一旦统一，就可以支持多种前端

这是非常重要的一个结果。

Graph 不应该绑定某一种 DSL。

例如用户可以：

```text
直接构造 Node / Edge
```

也可以以后提供：

```text
Stream façade
```

甚至可以从：

```text
配置
Schema
Machine
其他 DSL
```

生成 Graph。

也就是说：

```mermaid
flowchart TD
    A["Low-level Graph API"]
    B["Stream API"]
    C["Future DSL"]

    G["Typed Graph"]

    A --> G
    B --> G
    C --> G
```

只要最终统一到：

```text
Typed Graph
```

后面的：

```text
Validate
Optimize
Compile
Execute
```

都可以复用。

---

## 19. 这也是为什么 Stream 不应该是底层模型

后来我们会发现：

```text
Filter
Map
Reduce
Collect
```

非常适合提供类似 Java Stream 的接口。

但如果从一开始就把：

```text
Stream
```

定义成底层 runtime，那么系统很容易被限制在：

```text
线性 pipeline
```

而 Graph 天然可以表达：

```text
分支
合并
嵌套
Relation
Subgraph
```

所以更合理的关系是：

```text
Stream
    ↓
Graph façade
```

而不是：

```text
Graph
    ↓
复杂版 Stream runtime
```

---

## 20. Graph 也让 Branch 和 Relation 成为自然扩展

例如：

```text
一个输入
   ↓
两个分支
```

可以表示：

```mermaid
flowchart LR
    A["Input"]

    A --> B["Branch A"]
    A --> C["Branch B"]

    B --> D["Join"]
    C --> D
```

如果只使用线性 Stream，就必须另外发明：

```text
fork
join
zip
race
fallback
```

等大量特殊 runtime object。

Graph 中则可以进一步把这些抽象成：

```text
Subgraph
Relation
Coordination Policy
Completion Policy
Result Policy
Error Policy
```

从而减少高级模型数量。

---

## 21. “计算关系”开始成为真正的第一等数据

到这一步以后，可以保存：

```text
Graph
```

复制：

```text
Graph
```

比较：

```text
Graph
```

甚至：

```text
Graph -> Graph
```

做 transformation。

例如：

```text
原始 Graph
    ↓
Optimizer
    ↓
新 Graph
```

这实际上是：

> **程序对程序的计算。**

也是 Meta Programming 从：

```text
类型层
```

开始进入：

```text
程序结构层
```

的重要一步。

---

## 22. Optimizer 为什么会自然出现

假设 Graph：

```text
Map(f)
 ↓
Map(g)
```

如果：

```text
f : A -> B
g : B -> C
```

而且 Effects / Properties 允许，那么理论上可以考虑：

```text
Map(g ∘ f)
```

再例如：

```text
Map(identity)
```

可能可以删除。

```text
Filter(always_true)
```

也可能可以删除。

```text
Map(f)
 ↓
Map(f)
```

如果真正满足：

```text
f(f(x)) = f(x)
```

也可能简化。

这些优化之所以可能，不是因为：

```text
Graph 恰好长成这个样子
```

而是因为 Graph Node 已经保存：

```text
Type
Effects
Properties
Callable
```

因此 Optimizer 有足够的语义信息做决定。

---

## 23. Meta 信息开始第一次真正影响性能

这是一件很重要的事情。

之前：

```text
Type
Traits
Properties
```

看起来可能只是为了：

```text
更安全
更方便 reflection
```

但 Graph 出现以后，它们开始直接影响：

```text
是否允许优化
是否允许 fusion
是否允许并行
是否需要 adapter
```

也就是说：

```text
Metadata
```

第一次成为：

```text
Optimization Input
```

这说明前面建立的 Meta 层已经不再只是辅助工具。

它开始成为真正的：

```text
Compiler Semantic Substrate
```

---

## 24. Graph 还可以记录自己的版本

如果：

```text
Graph
```

可以被：

```text
修改
优化
clone
```

那么一个已经基于旧 Graph 生成的：

```text
Plan
Certificate
Optimization Trace
```

可能已经失效。

因此很自然需要：

```text
Graph Version
```

例如：

```text
Graph v42
```

生成：

```text
Plan v42
```

如果 Graph 后来变成：

```text
v43
```

那么：

```text
Plan v42
```

不能继续被当成：

```text
Graph v43 的证明
```

这让 Graph 从一个普通容器进一步具有：

```text
immutable snapshot / versioned semantic object
```

的意味。

---

## 25. 这里开始出现 Compiler 的完整轮廓

到这里，整个过程已经越来越像：

```text
Compiler
```

但输入不是传统语言源代码，而是一张通过 C API 构造出来的 Graph。

完整过程逐渐变成：

```mermaid
flowchart LR
    A["C DSL / API"]

    B["Surface Graph"]

    C["Validate"]

    D["Normalize"]

    E["Analyze"]

    F["Optimize"]

    G["Execution Form"]

    H["Execute"]

    A --> B --> C --> D --> E --> F --> G --> H
```

也就是：

```text
Front-end
IR
Validation
Lowering
Optimization
Backend
Execution
```

只是每一步都保持为：

```text
普通 C
有限模型
显式数据
```

---

## 26. 这就是 CFlow 真正开始形成的地方

做到这一阶段以后，系统已经不再只是：

```text
CMeta + Graph 数据结构
```

因为 Graph 需要：

```text
Operator
Lower
Optimizer
Executor
Runtime
```

一整套围绕：

```text
Flow of Computation
```

的能力。

这时候把这一部分独立出来，就非常自然。

因此：

```text
CMeta
```

继续负责：

```text
Type
Traits
Generic
Callable
Interface
Finite Relation
```

而新的一层：

```text
CFlow
```

开始负责：

```text
Graph
Operator
Dataflow
Lowering
Optimization
Execution
```

两者边界可以表示成：

```mermaid
flowchart TD
    M["CMeta"]

    M --> T["Type"]
    M --> C["Callable"]
    M --> E["Effects / Properties"]
    M --> I["Interface"]

    F["CFlow"]

    T --> F
    C --> F
    E --> F
    I --> F

    F --> G["Graph"]
    F --> O["Optimize"]
    F --> X["Execute"]
```

---

## 27. CFlow 最初仍然不是为了做 Java Stream

这点很重要。

有了：

```text
Graph
```

以后，第一目标仍然只是：

> **证明可以用 CMeta 描述一个复杂、真正可执行的计算对象。**

Stream 只是后来看到的一个非常自然的应用。

因为当 Graph 已经能够表达：

```text
Filter
Map
Reduce
Collect
```

以后，很容易发现：

```text
这和 Java Stream 的数据转换模型很像
```

于是才进一步考虑：

```text
能不能提供更高级、更友好的 Stream façade？
```

所以真正的发展顺序应该是：

```text
Typed Callable
    ↓
Typed Graph
    ↓
Data Transformation
    ↓
发现可以做 Stream
```

而不是：

```text
先决定模仿 Java Stream
    ↓
再设计 Graph
```

---

## 28. Graph 最大的价值可能不是“可以执行”，而是“可以先不执行”

这一点值得单独强调。

如果只是为了执行：

```text
f
 ↓
g
 ↓
h
```

普通 C 函数调用已经足够。

Graph 真正增加的能力是：

> **先把计算保存下来。**

因为先不执行，才有机会：

```text
检查
推导
分析
优化
选择 Backend
证明 transformation
```

所以 Graph 可以理解成：

```text
Deferred Computation Description
```

但它又不必像某些 lazy runtime 那样保留到每个 value 的执行阶段。

它可以在执行之前再次被：

```text
编译掉
```

---

## 29. 这也为后面的 Zero-Cost 路线创造了条件

一旦完整 Graph 已经知道：

```text
全部 Node
全部 Type
全部 Callable
全部拓扑
```

那么对于某些简单 Graph：

```text
Graph
```

本身甚至不应该进入 hot path。

例如：

```text
Filter
 ↓
Map
 ↓
Map
```

完全可能在后面 lowering 成：

```c
for (...) {
    if (!filter(x))
        continue;

    y = map1(x);
    z = map2(y);

    output[n++] = z;
}
```

也就是说：

```text
Graph
```

的存在不是为了让 runtime 更复杂。

恰恰相反。

它可能帮助我们：

> **在运行之前把高级抽象全部消掉。**

---

## 30. 从这里开始，问题变成“Graph 可以怎样执行”

做到 Graph 后，下一步真正有意思的问题不再是：

> Graph 还能增加什么 Node？

而是：

> **同一张 Graph 可以有哪些执行方式？**

例如：

```text
同步解释执行
```

或者：

```text
预编译成 Plan
```

或者：

```text
完全 lowering 成 Direct C
```

再或者：

```text
Publisher 暂时没有数据
需要 WAIT / Wake
```

于是执行模型开始分化。

这将自然引出：

```text
Stream
Reactive
Executor
Scheduler
```

等更高层能力。

---

# 小结：Graph 让“行为”进一步变成“程序结构”

前一章完成的是：

```text
Function
    ↓
Callable
```

让一个函数从：

```text
代码地址
```

变成：

```text
有类型、有语义的数据对象
```

这一章继续向前：

```text
Callable
    ↓
Graph
```

让多个行为之间的关系，也变成：

```text
普通数据
```

于是第一次可以：

```text
在执行之前看到整个计算
```

并对它进行：

```text
Validation
Type Propagation
Inference
Lowering
Optimization
Compilation
```

这就是 CFlow 真正出现的地方。

而 Graph 做出来以后，我们很快发现它特别适合表达：

```text
Filter
Map
FlatMap
Reduce
Collect
```

这一类数据转换。

也正是在这里，下一步才自然出现：

> **既然 Graph 已经能够很好地表达数据转换，能不能在它上面提供类似 Java Stream 的高级接口，让复杂的数据处理在 C 中也可以用更声明式、更类型化的方式表达？**

下一章将从这个发现继续：**从 Graph 到 Stream——为什么高级数据转换 API 可以只是 Graph 的一个轻量 façade，而不需要成为另一个重量级运行时。**
