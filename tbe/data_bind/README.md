# TurboUtils DataBind 2.0

DataBind 是 schema 驱动的纯 C 运行时。它解析 schema、构造动态值、校验字段，
并统一处理 TBE binary、JSON、YAML、XML 和 CSV。它不加载或生成运行时代码，
运行时也不要求 C/C++ 编译器。

`tbe_compiler` 与 DataBind 是两个不同层次：

- `tbe_compiler`：构建期工具，把 schema 渲染为 `.h/.c`。
- `DataBind`：运行时库，为动态对象、现有 C struct 映射和生成代码提供公共
  bind/serialization 引擎。

## 设计边界

DataBind 2.0 只定义两条强类型路线：

1. schema 生成 `.h/.c`，自动 bind、序列化和反序列化。
2. schema 映射现有 C struct，通过 `TBE_TYPED_*` 宏声明 descriptor，自动
   bind、序列化和反序列化。

动态 `DataBindObject` 是两条路线共用的格式中间层和宿主程序集成入口，不是
第三套 schema 契约。schema 始终是字段类型、wire layout 和外部名称的唯一事实源。

## Schema 注解标准

字段映射由 schema 注解表达：

```text
schema Trading [name("trading.v1")];

message Order {
  [name("orderId"), alias("id"), alias("order_id"), c(order_id)] uint64 id;
  [name("symbol")] string symbol;
}
```

- `[name("orderId")]`：规范外部名称；序列化始终输出它。
- `[alias("id")]`：仅用于反序列化输入，可重复。
- `[c(order_id)]`：生成代码中的 C 成员名；现有 struct descriptor 显式写成员。

调用普通 `data_bind_object_serialize_*()` 或 `tbe_typed_serialize()` 即应用映射，
不再存在单独的 `_mapped` 序列化入口。

## 路线一：生成 `.h/.c`

```powershell
tbe_compiler order.schema --lang c `
  --output generated/order.h `
  --source-output generated/order.c
```

生成头提供 owning struct 和类型化入口：

```c
#include "order.h"

DataBind *codec = NULL;
DataBindError error = DATA_BIND_ERROR_INIT;
Order_t order;
char *json = NULL;
size_t json_len = 0;

Order_init(&order);
if (Trading_codec_create(&codec, &error) != DATA_BIND_OK) return 1;
if (Order_from_json(codec, &order, input, input_len, &error) != DATA_BIND_OK) return 1;

printf("%llu\n", (unsigned long long)order.order_id);

if (Order_to_json(codec, &order, &json, &json_len, &error) != DATA_BIND_OK) return 1;
tbe_typed_serialized_free(json);
Order_clear(&order);
data_bind_free(codec);
```

### 编译静态库

```cmake
add_library(order_schema STATIC generated/order.c)
target_include_directories(order_schema PUBLIC generated)
target_link_libraries(order_schema PUBLIC TurboUtils::DataBind)
```

### 编译动态库

```cmake
add_library(order_schema SHARED generated/order.c)
target_compile_definitions(order_schema PRIVATE TBE_GENERATED_BUILD_SHARED)
target_include_directories(order_schema PUBLIC generated)
target_link_libraries(order_schema PUBLIC TurboUtils::DataBind)

target_compile_definitions(my_app PRIVATE TBE_GENERATED_USE_SHARED) # Windows consumer
target_link_libraries(my_app PRIVATE order_schema)
```

Linux/macOS shared library 构建也定义 `TBE_GENERATED_BUILD_SHARED`，生成头会设置默认
symbol visibility。静态库不定义这两个宏。

## 路线二：映射现有 C struct

```c
#include "tbe_typed.h"

typedef struct Order {
  uint64_t order_id;
  tstr_t symbol;
} Order;

TBE_TYPED_DEFINE_STRUCT(
    ORDER_BINDING, Order, "Order",
    TBE_TYPED_FIELD(Order, order_id, "id", TBE_TYPED_U64, TBE_TYPED_REQUIRED),
    TBE_TYPED_FIELD(Order, symbol, "symbol", TBE_TYPED_STRING, TBE_TYPED_REQUIRED));
```

应用先加载同一 schema，再验证 descriptor 并操作对象：

```c
DataBind *codec = NULL;
DataBindError error = DATA_BIND_ERROR_INIT;
Order order;
char *json = NULL;

if (data_bind_create("order.schema", &codec, &error) != DATA_BIND_OK) return 1;
if (tbe_typed_validate_schema(codec, "Order", &ORDER_BINDING, &error) != DATA_BIND_OK) return 1;
if (TBE_TYPED_BIND_INIT(ORDER_BINDING, &order, &error) != DATA_BIND_OK) return 1;
if (TBE_TYPED_BIND_PARSE(codec, ORDER_BINDING, "json", input, input_len, 0,
                         &order, &error) != DATA_BIND_OK) return 1;

printf("%llu\n", (unsigned long long)order.order_id);

if (TBE_TYPED_BIND_SERIALIZE(codec, ORDER_BINDING, &order, "json",
                             &json, NULL, &error) != DATA_BIND_OK) return 1;
tbe_typed_serialized_free(json);
TBE_TYPED_BIND_CLEAR(ORDER_BINDING, &order);
data_bind_free(codec);
```

这条路线不运行 `tbe_compiler`，也不生成业务头文件。宏 descriptor 将
`offsetof()`、成员类型、可选位和 wire 属性固化进普通 C 常量。

## RulesForge/TurboScript 如何使用

### 动态运行时模式

适合规则字段、脚本对象和运行时才知道的 schema：

```text
schema text/file
    -> data_bind_create[_from_text]()
    -> DataBindObject
    -> get/set/validate/serialize
```

部署只需要 schema 和 DataBind 静态/动态库。字段访问使用
`data_bind_object_get()` / `data_bind_value_get()`，无需外置编译器。

### 原生成员模式

需要 `order.id`、IDE 类型检查或固定 ABI 时：

```text
CI/构建阶段:
schema -> tbe_compiler -> order.h/order.c -> static/shared schema library

运行阶段:
RulesForge/TurboScript host -> 已编译 schema library -> DataBind runtime
```

C 编译器只出现在 CI、安装或发布阶段。生产进程不执行编译器。动态装载时可通过
生成的 `*_schema_codec()` 获取 `tbe_schema_codec_v1_t` provider，先校验 ABI，
再调用 provider。

## 动态对象示例

```c
DataBind *codec = NULL;
DataBindObject *order = NULL;
DataBindError error = DATA_BIND_ERROR_INIT;
char *output = NULL;
size_t output_len = 0;

if (data_bind_create("order.schema", &codec, &error) != DATA_BIND_OK) return 1;
if (data_bind_object_from_json(codec, "Order", input, input_len,
                               &order, &error) != DATA_BIND_OK) return 1;
if (data_bind_object_serialize_json(codec, order, &output,
                                    &output_len, &error) != DATA_BIND_OK) return 1;

data_bind_serialized_free(output);
data_bind_object_free(order);
data_bind_free(codec);
```

动态对象不能写成 C 表达式 `order.id`，因为 C 编译器不知道运行时 schema。宿主
语言应把 `order.id` 语法转换为动态属性查询；若必须得到真实 C 成员，只能选择
上述两条强类型路线之一。

## 线程与生命周期

- `DataBind` 创建完成后只读；并发共享是否允许由宿主在其边界统一约束。
- owning object/value 必须使用对应的 DataBind/TBE typed 释放函数。
- view 借用 owning record/object，owner 释放或清空后立即失效。
- typed object 在首次使用前调用 `*_init()`，结束时调用 `*_clear()`。
- 外部 schema 和输入必须在可信边界校验；解析错误直接返回，不自动修复或降级。

## 错误与 ABI

公开 API 返回 `DataBindStatus`，详细上下文写入 `DataBindError`。应用边界应记录
status、path 和 message，并停止使用失败输出。

```c
if (data_bind_abi_version() != DATA_BIND_ABI_VERSION) {
  /* 拒绝加载不匹配的运行时库。 */
}
```

DataBind 2.0 的 ABI 版本为 8。详细所有权与生成库边界见
[`RECORD_ABI.md`](RECORD_ABI.md)。
