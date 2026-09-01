# 第一章：CMeta 的起点——从写 C 宏的痛苦开始

C 是一门非常直接的语言。

一个函数就是一个函数，一个结构体就是一块明确的内存布局，一个函数指针就是一个地址。编译器做的事情相对透明，生成出来的程序也很容易和最终机器行为对应起来。

这种简单性，是 C 在系统软件、基础库、嵌入式和高性能程序中长期保持生命力的重要原因。

但这种简单性也有另外一面。

当程序规模开始变大以后，大量代码并不是因为算法复杂，而是因为：

> **同一种模式，需要针对不同类型、不同结构、不同函数重复写很多遍。**

例如一个库支持：

```c
int
long
float
double
```

如果每一种类型都需要：

```text
compare
hash
copy
destroy
format
```

那么很快就会出现大量结构几乎完全相同的函数。

例如：

```c
int int_compare(const int *a, const int *b)
{
    return (*a > *b) - (*a < *b);
}

int long_compare(const long *a, const long *b)
{
    return (*a > *b) - (*a < *b);
}

int float_compare(const float *a, const float *b)
{
    return (*a > *b) - (*a < *b);
}
```

这些代码本身并不难写。

真正麻烦的是维护。

如果以后修改比较规则，就要同时修改多个函数；如果增加一种类型，就要复制整套实现；如果其中一个版本忘记同步修改，程序仍然可能通过编译，却已经产生了细微的语义差异。

所以在 C 中，一个非常自然的反应就是：

> 既然这些代码结构一样，为什么不写成宏？

## 1. 第一个宏通常非常成功

例如前面的比较函数，可以立即写成：

```c
#define DEFINE_COMPARE(T, name)            \
    int name##_compare(                    \
        const T *a, const T *b)            \
    {                                      \
        return (*a > *b) - (*a < *b);      \
    }

DEFINE_COMPARE(int, int)
DEFINE_COMPARE(long, long)
DEFINE_COMPARE(float, float)
```

一下子，大量重复代码消失了。

这也是 C preprocessor 最吸引人的地方。

它不引入新的运行时。

它不需要另外安装代码生成器。

它不需要虚拟机，也不需要反射系统。

预处理完成以后，编译器看到的仍然只是普通 C：

```text
Macro
   ↓
Preprocess
   ↓
Ordinary C
   ↓
Compiler
   ↓
Machine Code
```

对于一个希望保持简单构建流程和低运行时开销的基础库来说，这种能力非常有价值。

于是接下来很自然：

```text
compare 可以写宏
hash 也可以
copy 也可以
destroy 也可以
```

问题看起来已经解决了。

但真正的麻烦才刚刚开始。

---

## 2. 宏消除了代码重复，宏调用自己又开始重复

假设现在我们已经有：

```c
DEFINE_COMPARE(...)
DEFINE_HASH(...)
DEFINE_COPY(...)
DEFINE_DESTROY(...)
```

那么代码可能变成：

```c
DEFINE_COMPARE(int, int)
DEFINE_COMPARE(long, long)
DEFINE_COMPARE(float, float)

DEFINE_HASH(int, int)
DEFINE_HASH(long, long)
DEFINE_HASH(float, float)

DEFINE_COPY(int, int)
DEFINE_COPY(long, long)
DEFINE_COPY(float, float)
```

函数实现已经没有重复了。

但是：

```text
int
long
float
```

这一组信息，又重复了一遍又一遍。

也就是说，第一个抽象解决了：

```text
重复的代码
```

但马上暴露出第二个问题：

```text
重复的事实
```

程序实际上只想表达一次：

> 我支持 `int`、`long`、`float` 这几种类型。

于是很自然地会继续抽：

```c
#define TYPES(X)       \
    X(int, int)        \
    X(long, long)      \
    X(float, float)
```

然后：

```c
TYPES(DEFINE_COMPARE)
TYPES(DEFINE_HASH)
TYPES(DEFINE_COPY)
```

这一步看起来仍然只是一个普通的 X-Macro 技巧，但它实际上发生了一个非常重要的变化。

一开始宏复用的是：

```text
代码模板
```

现在开始复用的是：

```text
数据
```

也就是：

```text
有哪些类型
```

这意味着宏已经不只是 source replacement。

它开始承担一种非常轻量的：

```text
compile-time description
```

功能。

---

## 3. 从“复用代码”变成“复用事实”

这种方式一旦开始使用，很快就会扩展到其他地方。

例如枚举：

```c
#define STATES(X)             \
    X(READY,   "ready")       \
    X(RUNNING, "running")     \
    X(DONE,    "done")
```

它可以同时用于生成：

```text
enum
字符串
解析表
调试输出
```

结构体字段也可以采用类似方式：

```c
#define USER_FIELDS(X) \
    X(int, id)         \
    X(double, score)
```

然后生成：

```text
field declaration
field name
offset
size
serialization mapping
```

函数签名也可以维护成一组事实：

```text
input type
output type
function name
```

于是很快会看到越来越多这种东西：

```text
TYPE_LIST
FIELD_LIST
ENUM_LIST
FUNCTION_LIST
SIGNATURE_LIST
```

它们看起来用途不同，但其实开始表现出一种共同形式：

```text
一组有限的记录
        ↓
交给不同的宏
        ↓
生成不同结果
```

这时，宏的角色已经开始变化。

它不再只是：

> 把某段文字复制到几个地方。

而逐渐变成：

> **用一份声明描述事实，再从这些事实生成程序。**

---

## 4. 然而 C preprocessor 并不适合做这种事情

问题在于，C preprocessor 最初并不是为这种复杂用途设计的。

它最擅长的是：

```c
#define BUFFER_SIZE 1024
```

或者：

```c
#define MAX(a, b) ...
```

但一旦开始真正构造可复用的宏基础设施，就会不断碰到各种很不自然的规则。

最典型的例子是 token 拼接。

看起来应该很简单：

```c
#define CAT(a, b) a##b
```

但假设：

```c
#define TYPE int
```

然后：

```c
CAT(TYPE, _value)
```

如果不了解参数展开规则，就会很容易得到与直觉不同的结果。

所以通常还要多写一层：

```c
#define CAT_I(a, b) a##b
#define CAT(a, b) CAT_I(a, b)
```

只是为了：

> **让参数先展开，再拼接。**

一个普通语言中几乎不会成为问题的事情，在 preprocessor 中已经需要理解特殊展开顺序。

---

## 5. 逗号甚至会改变“类型”的含义

另一个非常典型的问题是宏参数。

例如：

```c
MACRO(int)
```

当然没有问题。

但是如果传入一个包含逗号的表达式或者类型描述：

```text
(A, B)
```

preprocessor 会天然把它理解成：

```text
两个参数
```

而不是：

```text
一个结构
```

于是开始需要：

```text
括号
tuple
UNPAREN
```

例如：

```c
#define UNPAREN(...) __VA_ARGS__
```

然后又会碰到：

> 怎么知道输入是不是括号？

于是开始出现：

```text
PROBE
IS_PROBE
IS_PAREN
SECOND
```

这时候事情已经很有意思了。

最开始我们只是想：

> 少写几个重复函数。

现在却开始研究：

> **如何在预处理阶段识别一个 token sequence 的结构。**

---

## 6. 连 `for each` 都要自己造

如果已经有：

```c
#define TYPES(X) \
    X(int)       \
    X(long)      \
    X(float)
```

这种写法还比较直接。

但如果数据来自 variadic arguments，例如：

```c
FOR_EACH(M,
    int,
    long,
    float,
    double)
```

C preprocessor 并没有真正的：

```text
for
foreach
iterator
```

所以只能自己展开。

典型方式是：

```c
#define FOR_EACH_1(...)
#define FOR_EACH_2(...)
#define FOR_EACH_3(...)
#define FOR_EACH_4(...)
```

然后再写：

```text
NARG
```

计算参数数量。

再做：

```text
CAT(FOR_EACH_, N)
```

最终选择正确的：

```text
FOR_EACH_4
```

也就是说，一个非常普通的需求：

> 对四个值做相同操作。

在宏世界里已经涉及：

```text
variadic arguments
argument counting
token concatenation
dispatch
expansion ordering
```

这时问题已经不是：

> 能不能实现？

大多数情况下都能。

真正的问题开始变成：

> **为了实现这些很普通的复用，复杂度是不是已经太高了？**

---

## 7. 更麻烦的是，宏很难嵌套

即使终于实现了：

```text
FOR_EACH
```

很快又会需要：

```text
FOR_EACH
    ↓
里面再 FOR_EACH
```

例如：

```text
遍历输入类型
    ↓
对每个输入类型再遍历输出类型
```

直觉上，这是普通的嵌套循环。

但 C preprocessor 有一个非常特殊的规则：

> 一个宏正在展开时，同名宏会被暂时禁止继续展开。

于是：

```text
FOR_EACH(FOR_EACH(...))
```

并不一定按照普通递归或嵌套调用的方式工作。

为了解决这个问题，通常只能准备不同的 expansion family：

```text
FOR_EACH_A
FOR_EACH_B
FOR_EACH_C
```

外层用 A，内层用 B，再下一层用 C。

这看起来并不优雅，但它足够确定，也更容易跨编译器工作。

这也逐渐形成了一个重要认识：

> **与其试图把 preprocessor 变成一门万能语言，不如承认它的边界，并在明确边界内建立可靠工具。**

---

## 8. 宏最痛苦的地方往往不是写，而是错

复杂宏还有一个比语法更大的问题：

```text
错误信息
```

普通 C 中：

```c
int x = "hello";
```

编译器通常可以直接告诉你类型错误。

但复杂宏出错时，真正写错的位置可能是：

```c
Traits(User,
    (equal, user_equal),
    (hash, user_hash)
);
```

最终编译器报出的却可能是：

```text
expected ')' before ...
```

或者：

```text
unknown identifier ...
```

真正的展开路径可能已经变成：

```text
输入声明
   ↓
row unpack
   ↓
foreach
   ↓
tag dispatch
   ↓
token concat
   ↓
generated declaration
   ↓
C compiler error
```

也就是说：

> **用户写错的是第一层，编译器看到的却是第六层。**

所以宏越复杂，一个新的设计要求就越重要：

> 不仅要考虑宏能不能工作，还要考虑它失败时能不能理解。

---

## 9. 但真正最大的限制不是这些，而是“宏没有类型”

前面的所有问题虽然麻烦，但都还有办法处理。

真正开始限制能力的是另一件事：

> **preprocessor 根本不知道什么是类型。**

对 C compiler 来说：

```c
int
double
User
```

是完全不同的类型。

但对 preprocessor 来说，它们只是：

```text
token
token
token
```

它不知道：

```text
int 是整数
double 是浮点数
User 是结构体
```

更不知道：

```text
sizeof(T)
alignment
copy semantics
destroy semantics
hashability
comparability
```

它也不知道：

```text
int -> double
```

表示一个函数签名。

更不知道：

```text
Vec<int>
```

表示一个以 `int` 为参数的 Generic type。

因此只依靠普通宏时，很多所谓“类型处理”实际上都是在模拟：

```text
INT
DOUBLE
USER
```

然后手工维护：

```text
INT -> int
INT -> integer
INT -> descriptor
INT -> hash
INT -> compare
```

这又回到了最开始的问题：

> **同一个事实再次被写了很多遍。**

---

## 10. C11 的 `_Generic` 打开了一道门

C11 提供了一个非常重要的能力：

```c
_Generic
```

它虽然远远不是 C++ template，但它可以完成有限的：

```text
type-directed selection
```

例如概念上：

```c
_Generic((value),
    int:    handle_int,
    double: handle_double)
```

这意味着，如果已经有一份：

```text
有限类型列表
```

就有可能从这份列表生成：

```text
类型选择
descriptor lookup
callback signature
```

也就是说：

```text
宏
```

第一次开始可以和：

```text
C compiler 的类型系统
```

产生真正联系。

这是一个非常关键的变化。

---

## 11. 一旦加入类型，很多问题 suddenly become structured

假设一个类型不仅有：

```text
name
```

还能够描述：

```text
size
alignment
kind
traits
identity
```

那么很多过去必须靠命名约定的事情，就开始变成真正的类型关系。

例如：

```text
HashMap<K,V>
```

可以要求：

```text
K supports hash
K supports equal
```

排序结构可以要求：

```text
K supports compare
```

一个函数可以具有：

```text
input type
output type
```

而不仅仅是一个地址。

这意味着系统开始从：

```text
text generation
```

逐渐向：

```text
type-aware generation
```

演化。

但这一步不是一开始规划好的。

它只是不断解决宏复用问题之后，非常自然地走到这里。

---

## 12. 第一阶段真正得到的经验

回头看这一过程，会发现每一层抽象都不是因为：

> 想增加新语法。

而是因为上一层已经出现新的重复。

```text
重复代码
    ↓
参数化宏

重复宏调用
    ↓
共享列表

重复列表消费方式
    ↓
通用 foreach / repeat

重复 row 表示
    ↓
统一 schema

重复类型知识
    ↓
类型化描述
```

整个过程可以概括成：

```text
C code
  ↓
Macro reuse
  ↓
Macro data
  ↓
Structured rows
  ↓
Reusable preprocessor primitives
  ↓
Type-aware description
```

真正推动这一过程的始终不是：

```text
“我们还可以做什么炫酷的宏？”
```

而是：

> **怎样少维护一份相同的知识。**

---

## 13. 一个后来一直没有改变的原则

经历了这些问题以后，一个原则逐渐变得非常明确：

> **宏的价值不是能力越强越好，而是能否用最小的复杂度，把已经存在的重复结构收敛成一个事实源。**

因此后来的设计始终倾向：

```text
finite
explicit
bounded
predictable
```

而不是：

```text
unbounded recursion
universal template language
macro interpreter
```

因为最初使用宏的目的，就是为了：

```text
降低复杂度
```

如果最后为了宏而创造出一个比 C 更复杂、更难调试的系统，就已经失去了最初的意义。

---

## 14. 下一步：当宏开始拥有类型

到这一阶段，我们已经有了两个非常重要的基础：

第一，知道怎样让：

```text
一组事实
```

被不同代码反复使用。

第二，开始能够把：

```text
C type
```

和：

```text
compile-time description
```

联系起来。

接下来的问题自然变成：

> **既然已经能够描述类型，那么能不能让宏根据类型生成不同代码？**

进一步：

> 能不能描述 Generic？

再进一步：

> 能不能从几个已知类型，推导一个新的类型？

也就是从：

```text
Reuse
```

进入：

```text
Generate
```

再进一步进入：

```text
Derive
```

而正是在这个阶段，原本零散的宏实践才开始逐渐形成一套真正的类型化元编程模型。

下一章将从这里继续：**宏如何从“文本复用”进入类型、Traits、Generic 和有限类型推导。**