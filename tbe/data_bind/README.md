# DataBind 公共 C API

`tbe/data_bind` 是 TurboUtils schema 绑定功能面向第三方的集成边界。它通过
不透明句柄和动态值访问器提供 C ABI。除非需要嵌入 TurboUtils 脚本，否则
使用方不应依赖 `modules/data_bind` 或 `exprtk`。

本文的说明文字统一使用简体中文；API 标识符、源码符号、命令、协议名称，
以及程序实际输入输出等技术字面量按契约保留原文。

`modules/parser` 使用本库完成面向 TurboUtils 的 schema 绑定、严格布尔校验和
schema 反射。Parser 仍负责脚本值转换和详细的 `validate_ex` 诊断。DataBind
负责 JSONPath、XMLPath 和 CSVPath 等格式感知路径查询 API。
`tbe/data_bind` 内部的格式解析直接使用 TurboNet::Parser，不会调用
`modules/parser`。

## 安全注意事项

**Schema 信任边界：** DataBind 通过 MIR 使用 JIT 编译，根据 schema 定义生成
优化后的二进制解析器。codec 创建过程假定 **schema 文件来自可信来源**。

DataBind 包含针对常见问题的校验，例如字段嵌套深度超过 32 层、偏移超过
1GB、字段总数超过 10,000 以及循环类型引用，但它不能全面防御所有恶意
schema 构造。

**最佳实践：**

- 仅从可信位置加载 schema 文件，例如应用程序包、已验证的配置目录或经过
  身份认证的远程来源。
- 不要根据用户提供或不可信的 schema 定义创建 codec。
- 在多租户环境中隔离 schema 管理，并在创建 codec 前验证 schema 来源。
- 二进制 TBE payload 解析使用 JIT 编译代码；应在应用边界正确校验输入
  数据来源。

**校验限制：**

- 字段最大嵌套深度：32 层
- 字段最大偏移：1GB（1,073,741,824 字节）
- 每个 message 的最大字段总数：10,000
- 最大嵌套类型跟踪数：256 个不同类型

创建 codec 时超过这些限制会返回 `DATA_BIND_ERR_SCHEMA`，并在
`DataBindError` 中提供描述性消息。

## CMake

安装 TurboUtils 后：

```cmake
find_package(TurboUtils CONFIG REQUIRED)

add_executable(app main.c)
target_link_libraries(app PRIVATE TurboUtils::DataBind)
```

在 Windows 上，`TurboUtils::DataBind` 会为使用方定义 `DATA_BIND_USE_DLL`。
不要在库自身以外定义 `DATA_BIND_BUILD_DLL`。

## 最小使用示例

```c
#include "data_bind.h"
#include <string.h>

DataBind *codec = NULL;
DataBindError err = DATA_BIND_ERROR_INIT;
DataBindStatus status = data_bind_create("orders.tbe", &codec, &err);
if (status != DATA_BIND_OK) {
  return 1;
}

const char *json = "{\"id\":7,\"symbol\":\"ABCD\"}";
DataBindValue *order = NULL;
status = data_bind_parse_json(codec, "Order", json, strlen(json), &order, &err);
if (status != DATA_BIND_OK) {
  data_bind_free(codec);
  return 1;
}

const DataBindValue *id = data_bind_value_get(order, "id");
int32_t value = 0;
if (data_bind_value_get_int32(id, &value) != DATA_BIND_OK) {
  data_bind_value_free(order);
  data_bind_free(codec);
  return 1;
}

data_bind_value_free(order);
data_bind_free(codec);
```

## 所有权

- `DataBind*` 归调用方所有，使用 `data_bind_free` 释放。
- 返回的 `DataBindValue*` 归调用方所有，使用 `data_bind_value_free` 释放。
- 返回的 `DataBindObject*` 拥有自己的类型名和值树，使用
  `data_bind_object_free` 释放；它不会保留 schema codec。
- `data_bind_object_serialize_bin` 返回的二进制缓冲区使用
  `data_bind_binary_free` 释放。
- `data_bind_object_serialize_json`、`_yaml`、`_xml` 或 `_csv` 返回的文本
  使用 `data_bind_serialized_free` 释放。
- `data_bind_value_clone` 对完整值树进行独立深拷贝。成功后副本归调用方
  所有，源对象所有权不变；失败时输出为 `NULL`。当重试、扇出、队列或
  其他生命周期边界需要独立绑定值时，应使用此 API。
- 访问器返回的 `const char*`、子 `DataBindValue*` 和 map 项均为借用视图。
  当所属 `DataBindValue` 或 `DataBindObject` 被释放，或者提供借用 record
  的回调返回后，这些视图立即失效。
- 可以安全地向 `data_bind_free` 和 `data_bind_value_free` 传入 `NULL`。

## 动态值、`std::any` 与指针

`DataBindValue` 是指向带标签递归值树的不透明指针。它更接近受 schema
约束的 `std::variant`，而不是 `std::any`：

| 能力 | `DataBindValue` | C++ `std::any` |
|---|---|---|
| 运行时类型检查 | `data_bind_value_kind()` | `type()` / `any_cast` |
| 可存储类型 | 封闭的 `DataBindValueKind` 集合 | 任意可复制构造的 C++ 类型 |
| 嵌套对象/列表/集合/映射 | 内置 | 由应用定义 |
| 模式定义校验 | 内置 | 无 |
| JSON/YAML/XML/CSV/二进制转换 | 模式定义和格式支持时内置 | 本身不提供 |
| 任意 `void*` 或 `MyClass*` 值 | 不支持 | 可在 `std::any` 中保存指针值 |

封闭类型集合包括 null、有符号/无符号整数、double、bool、string、bytes、
UUID、时间值、decimal、bigint、money、object、list、set 和 map。用于持有
字符串、字节和子容器的内部指针属于实现细节，并不意味着
`DataBindValue` 可以作为任意指针容器。名为 `void *user` 的回调参数是
调用方上下文，不属于可序列化值树。

访问数据时使用借用指针：

```c
const DataBindValue *root = data_bind_object_value(object);
const DataBindValue *field = data_bind_value_get(root, "name");
const char *name = NULL;
size_t name_len = 0;

DataBindStatus status =
    data_bind_value_get_string(field, &name, &name_len);
```

所有权链如下：

```text
DataBindObject                         归调用方所有
└── const DataBindValue *root          借用
    └── const DataBindValue *field     借用
        └── const char *name           借用
```

调用 `data_bind_object_free(object)` 后，上述所有借用指针都会失效。在值
跨越所有者、队列、重试、回调或异步生命周期边界前，应先克隆：

```c
DataBindValue *copy = NULL;
DataBindStatus status = data_bind_value_clone(field, &copy);
if (status == DATA_BIND_OK) {
  /* 此处的 copy 拥有独立所有权。 */
}
data_bind_value_free(copy);
```

不可序列化的运行时对象应保留在 DataBind 之外。C++ 应用可以将
`DataBindObject` 与 `std::any` 配对；C 代码可以维护从稳定 schema ID 到
应用所有不透明指针的旁表。不要序列化裸地址：它只在当前进程中有效，
无法重建，也不存在可移植的克隆、析构或所有权语义。

### 推荐的运行时附件模式

当一个运行时附件与一个 `DataBindObject` 具有相同生命周期时，使用应用层的
小型不透明封装对象。不要向 `DataBindValue` 添加指针类型。以下是
使用方代码的设计示例，不是 DataBind 导出的 API：

```c
/* runtime_envelope.h：应用层接口 */
#ifndef RUNTIME_ENVELOPE_H
#define RUNTIME_ENVELOPE_H

#include "data_bind.h"
#include <stdint.h>

typedef struct RuntimeEnvelope RuntimeEnvelope;
typedef void (*RuntimeAttachmentDestroyFn)(void *attachment);

RuntimeEnvelope *runtime_envelope_create(
    DataBindObject *object,
    uint64_t attachment_type,
    void *attachment,
    RuntimeAttachmentDestroyFn destroy);

const DataBindObject *runtime_envelope_object(
    const RuntimeEnvelope *envelope);

void *runtime_envelope_attachment(
    RuntimeEnvelope *envelope,
    uint64_t expected_type);

void runtime_envelope_free(RuntimeEnvelope *envelope);

#endif
```

创建成功后，封装对象同时接管 `object` 和 `attachment` 的所有权。分配或
校验失败时，所有权仍归调用方。`runtime_envelope_free()` 恰好调用一次
附件析构函数，然后调用 `data_bind_object_free()`：

```c
/* runtime_envelope.c：应用层实现 */
#include "runtime_envelope.h"
#include <stdlib.h>

struct RuntimeEnvelope {
  DataBindObject *object;
  uint64_t attachment_type;
  void *attachment;
  RuntimeAttachmentDestroyFn destroy;
};

RuntimeEnvelope *runtime_envelope_create(
    DataBindObject *object,
    uint64_t attachment_type,
    void *attachment,
    RuntimeAttachmentDestroyFn destroy) {
  RuntimeEnvelope *envelope;
  if (object == NULL) return NULL;
  envelope = (RuntimeEnvelope *)malloc(sizeof(*envelope));
  if (envelope == NULL) return NULL;
  envelope->object = object;
  envelope->attachment_type = attachment_type;
  envelope->attachment = attachment;
  envelope->destroy = destroy;
  return envelope;
}

const DataBindObject *runtime_envelope_object(
    const RuntimeEnvelope *envelope) {
  return envelope != NULL ? envelope->object : NULL;
}

void *runtime_envelope_attachment(
    RuntimeEnvelope *envelope,
    uint64_t expected_type) {
  if (envelope == NULL || envelope->attachment_type != expected_type) return NULL;
  return envelope->attachment;
}

void runtime_envelope_free(RuntimeEnvelope *envelope) {
  if (envelope == NULL) return;
  if (envelope->destroy != NULL) {
    envelope->destroy(envelope->attachment);
  }
  data_bind_object_free(envelope->object);
  free(envelope);
}
```

序列化有意忽略附件：

```c
const DataBindObject *object = runtime_envelope_object(envelope);
DataBindStatus status =
    data_bind_object_serialize_json(object, &json, &json_len, &error);
```

### 反序列化与运行时附件重建

DataBind 支持把 schema 约束的数据反序列化为拥有所有权的
`DataBindObject`。输入格式与入口如下：

| 输入格式 | 反序列化入口 | 额外要求 |
|---|---|---|
| TBE 二进制 | `data_bind_object_from_bin()` | 编解码器、模式类型名和匹配的二进制数据 |
| JSON | `data_bind_object_from_json()` | 编解码器和模式类型名 |
| YAML | `data_bind_object_from_yaml()` | 编解码器和模式类型名 |
| XML | `data_bind_object_from_xml()` | 编解码器和模式类型名 |
| CSV | `data_bind_object_from_csv()` | 编解码器、模式类型名和显式行号 |

MIR/BMIR 本身不是业务数据，也不会直接生成业务对象。它先恢复正常的
`DataBind` codec，再由 `data_bind_object_from_*()` 处理业务数据：

```text
精确 schema 字节 + 匹配的 BMIR
    -> data_bind_create_from_bmir()
    -> DataBind codec

DataBind codec + schema 类型名 + 二进制/JSON/YAML/XML/CSV
    -> data_bind_object_from_*()
    -> DataBindObject
```

例如，从预生成 BMIR 加载 codec 并反序列化二进制对象：

```c
DataBind *codec = NULL;
DataBindObject *object = NULL;
DataBindError error = DATA_BIND_ERROR_INIT;

DataBindStatus status = data_bind_create_from_bmir(
    schema_text, schema_len, bmir_data, bmir_len, &codec, &error);
if (status == DATA_BIND_OK) {
  status = data_bind_object_from_bin(
      codec, "Packet", wire_data, wire_len, &object, &error);
}

if (status == DATA_BIND_OK) {
  const DataBindValue *root = data_bind_object_value(object);
  const DataBindValue *name_value = data_bind_value_get(root, "name");
  const char *name = NULL;
  size_t name_len = 0;

  status = data_bind_value_get_string(name_value, &name, &name_len);
  /* name 是借用视图，只在 object 释放前有效。 */
}

data_bind_object_free(object);
data_bind_free(codec);
```

`data_bind_object_from_*()` 成功后，调用方拥有返回的 `DataBindObject`，
并负责调用 `data_bind_object_free()`。该对象不持有 codec，因此 codec
释放后仍可访问对象字段；但若要重新输出 TBE 二进制，仍须向
`data_bind_object_serialize_bin()` 提供包含匹配 schema 类型的 codec。

运行时附件不能采用相同方式反序列化。裸指针地址只在原进程和
原对象生命周期内有意义，不能持久化、跨进程传输或在进程重启后恢复。
需要恢复附件时，只把稳定 ID 写入模式定义：

```text
message Request {
  uint64 request_id;
  string attachment_id;
}
```

推荐的恢复流程是：

```text
持久化或网络数据
    -> DataBindObject
    -> 读取 attachment_id
    -> 应用层解析器 / 注册表 / 工厂
    -> 获取或重建运行时附件
    -> RuntimeEnvelope(object, attachment)
```

解析器、注册表和工厂属于应用层，不是 DataBind 公开 API。它们必须定义
附件的所有权协议：独占资源使用明确的 `destroy` 回调；共享资源使用
`retain`/`release` 或等价引用计数。不要把注册表中的借用指针包装成由
`RuntimeEnvelope` 独占释放的指针。

整个恢复过程应当快速失败，并遵守以下清理顺序：

| 失败阶段 | 必须执行的清理 |
|---|---|
| 编解码器或载荷反序列化失败 | 保持输出对象为 `NULL`；若编解码器是本流程临时创建的，则释放它 |
| 稳定 ID 缺失、类型错误或业务校验失败 | 释放 `DataBindObject` |
| 解析器查找或重建失败 | 释放 `DataBindObject`，不得返回半初始化封装对象 |
| 附件已获得，但封装对象创建失败 | 按附件协议执行 `release`/`destroy`，再释放 `DataBindObject` |
| 封装对象创建成功 | 所有权按 `runtime_envelope_create()` 契约转移给封装对象 |

因此，DataBind 负责可序列化载荷的类型化反序列化；应用层负责根据模式定义
中的稳定 ID 恢复进程内资源。二者分离可以保持 wire 格式、对象
所有权和 DataBind ABI 稳定。

根据生命周期选择附件边界：

| 运行时需求 | 推荐表示方式 |
|---|---|
| 一个对象独占一个附件 | 带 `destroy` 回调的不透明 `RuntimeEnvelope` |
| 多个对象共享一个资源 | 模式定义中的稳定 ID 加应用注册表 |
| 持久化或 IPC 后重建 | 序列化稳定 ID，绑定完成后解析该 ID |
| 仅限 C++ 的进程内状态 | 包含 `DataBindObject` 和 `std::any` 或 `std::shared_ptr` 的 RAII 包装器 |
| DLL 或插件 ABI | 带显式 `destroy` 或 `retain`/`release` 函数的纯 C 不透明句柄 |

基础封装对象有意设计为不可克隆。若使用方需要克隆，应增加显式的
附件克隆回调；未提供克隆操作时返回错误。不得静默共享或浅拷贝附件。

不推荐添加 `DATA_BIND_VALUE_OPAQUE`：所有克隆路径和格式适配器都需要定义
新行为，裸地址仍然无法跨越进程边界，而且所有权和析构语义会进入公共
DataBind ABI。将封装对象保留在应用层可以保持现有 schema、wire 格式、
序列化器和 DataBind ABI 不变。

## 错误处理

公共 ABI 使用 `DataBindStatus` 返回值和调用方持有的 `DataBindError` 存储。
创建或解析值的函数成功时返回 `DATA_BIND_OK`，并通过输出参数交付拥有
所有权的结果：

```c
DataBindError err = DATA_BIND_ERROR_INIT;
DataBindValue *value = NULL;
DataBindStatus status =
  data_bind_parse_json(codec, "Order", json, strlen(json), &value, &err);
if (status != DATA_BIND_OK) {
  fprintf(stderr, "%s: %s\n", data_bind_status_name(status), err.message);
}
```

`DataBindError` 包含 `code`、`line`、`column`、`path` 和 `message`。消息
缓冲区属于调用方提供的结构体，因此 codec 函数返回后仍可安全读取。

### 错误码映射

所有解析函数在不同输入格式间使用一致的错误码：

| 错误码 | 二进制解析 | JSON 解析 | CSV 解析 | XML 解析 |
|------------|--------------|------------|-----------|-----------|
| `DATA_BIND_OK` | 成功 | 成功 | 成功 | 成功 |
| `DATA_BIND_ERR_INVALID_ARG` | 编解码器/缓冲区为 NULL | 编解码器/JSON 为 NULL | 编解码器/CSV 为 NULL | 编解码器/XML 为 NULL |
| `DATA_BIND_ERR_TYPE_NOT_FOUND` | 未知类型 | 未知类型 | 未知类型 | 未知类型 |
| `DATA_BIND_ERR_PARSE` | 二进制解码失败 | JSON 解析失败 | CSV 解析失败 | XML 解析失败 |
| `DATA_BIND_ERR_TYPE_MISMATCH` | 字段类型错误 | 值类型错误 | 单元格类型错误 | 节点类型错误 |
| `DATA_BIND_ERR_SCHEMA` | 模式定义校验失败 | 模式定义不匹配 | 模式定义不匹配 | 模式定义不匹配 |
| `DATA_BIND_ERR_RUNTIME` | JIT 运行时错误 | 绑定错误 | 绑定错误 | 绑定错误 |
| `DATA_BIND_ERR_OOM` | 内存不足 | 内存不足 | 内存不足 | 内存不足 |

### 错误路径格式

`DataBindError.path` 使用格式特定的位置标识符，以提供一致的错误报告：

- **二进制 TBE**：`"binary: parse failed"`；支持详细位置跟踪时使用
  `"binary: offset N"`
- **JSON**：嵌套字段使用 JSONPath 风格的 `"json: $.field.path"`
- **CSV**：基于表头的位置使用 `"csv: row R col C"` 或
  `"csv: row R field_name"`
- **XML**：使用 XMLPath 风格的 `"xml: /root/element[@attr]"`

错误输出示例（字符串内容为当前诊断字面量，按原样保留）：

```c
// 二进制解析错误
err.path = "binary: parse failed"
err.message = "Binary bind failed for type: Order"

// JSON 解析错误
err.path = "json: $.items[2].price"
err.line = 15
err.column = 12
err.message = "Expected number, got string"

// CSV 解析错误
err.path = "csv: row 42 col 5"
err.line = 42
err.message = "Invalid date format in field 'orderDate'"

// XML 解析错误
err.path = "xml: /orders/order[3]/status"
err.message = "Unknown enum value: 'PENDING'"
```

## 支持的输入

- `data_bind_parse`：解析二进制 TBE payload。
- `data_bind_parse_json`：解析与 schema 类型匹配的 JSON 对象或标量。
- `data_bind_parse_json_all`：JSON 数组逐项绑定；非数组绑定为单项 list。
- `data_bind_parse_csv`：解析带表头行的 CSV。嵌套字段使用
  `header.seq`、`bids[0].price` 和 `attrs.x` 等路径。
- `data_bind_parse_csv_all`：绑定所有已解析的 CSV 行。
- `data_bind_parse_xml`：将 XML 文档根节点绑定到请求的 schema 类型。
  record 字段优先从同名子元素绑定，其次从同名属性绑定；重复同名元素
  绑定为 list/group。
- `data_bind_parse_json_path`：绑定第一个匹配 JSONPath 表达式的值。
- `data_bind_parse_json_path_all`：绑定所有匹配 JSONPath 表达式的值。
- `data_bind_parse_xml_path_all`：使用 XMLPath 选择节点并逐一绑定。可使用
  内置 TurboNet XML 解析器支持的表达式，例如 `//order`。
- `data_bind_parse_csv_path`：绑定匹配 CSVPath 表达式的行。
- 流构造函数直接编码绑定语义：
  `data_bind_stream_json_create`, `data_bind_stream_json_all_create`,
  `data_bind_stream_json_path_create`,
  `data_bind_stream_json_path_all_create`,
  `data_bind_stream_yaml_create`, `data_bind_stream_yaml_all_create`,
  `data_bind_stream_yaml_path_create`,
  `data_bind_stream_yaml_path_all_create`,
  `data_bind_stream_csv_all_create`, `data_bind_stream_csv_path_create`,
  `data_bind_stream_xml_create` 和
  `data_bind_stream_xml_path_all_create`。不存在独立的流模式或选项对象。
  JSON 根数组流增量绑定各项，并保留精确的有符号/无符号 64 位数字 token；
  其他 JSONPath 表达式通过 SAX 增量校验数据块，并在 `finish` 时构建 DOM。
  CSV 流增量处理完整 record，并在表头行之后编译 CSVPath。XML 流增量绑定
  不重叠的 `//name` 元素匹配；其他 XMLPath 表达式通过 SAX 增量校验数据块，
  并在 `finish` 时构建 DOM。YAML 流也通过 SAX 增量校验语法，然后在
  `finish` 时执行 schema 和 YPATH 绑定。
- `data_bind_stream_feed_file`：按固定大小读取文件块，并送入已有 stream。
  拥有异步 I/O 循环的应用应通过 `data_bind_stream_feed` 提交已完成的
  buffer；DataBind 不重复提供调度器或异步文件 API。
- `data_bind_stream_set_record_callback`：同步接收每个经路径选择且完成
  schema 绑定的 record。根数组 JSON、CSV 行和 `//order` 等简单 XML 后代
  路径在 `feed` 中交付；需要完整文档的路径在 `finish` 中交付。回调 record
  只在回调期间借用。返回 `DATA_BIND_RECORD_STOP` 会停止后续回调，但不会
  停止校验或最终值构建；返回 `DATA_BIND_RECORD_ERROR` 会使 stream 失败。
- `data_bind_validate_json_path`：按 schema 类型校验第一个 JSONPath 匹配项。
- `data_bind_validate_csv_path`：校验所有匹配 CSVPath 表达式的 CSV 行。
- `data_bind_validate_xml_path`：严格校验文档根节点或给定 XMLPath 选中的
  每个节点。
- `data_bind_validate_json`：严格校验 JSON。只有每一项都能绑定到请求的
  schema 类型时，数组才有效。
- `data_bind_validate_csv`：按请求的 schema 类型严格校验每个 CSV 数据行。
  CSV 使用与 CSV 绑定相同的表头路径规则。
- `data_bind_create_from_text`：直接根据内存中的 schema 文本创建 codec。

`parse_*_all` API 是绑定辅助函数：它们返回成功绑定值组成的 list，并可能
跳过无效的数组项或 CSV 行。当调用方需要严格的全有或全无输入校验时，使用
`data_bind_validate_json`、`data_bind_validate_csv`、
`data_bind_validate_json_path`, `data_bind_validate_csv_path`,
`data_bind_validate_xml_path`。

## 文本输出

拥有所有权的对象可以序列化为 JSON、YAML、XML 或 CSV。对应的
`data_bind_object_write_*` 函数通过调用方回调交付完整序列化文档。

JSON 和 YAML 将有符号及无符号 64 位值输出为精确数字 token，包括超出
IEEE-754 精确整数范围的值。XML 和 CSV 使用规范十进制文本。UUID 值在所有
文本格式中均使用规范 UUID 字符串，并可重新绑定为
`DATA_BIND_VALUE_UUID` / `turbo_uuid_t`。

CSV 输出包含一个 RFC 4180 表头行和一个数据行。它使用 CSV 绑定器接受的
相同路径：嵌套对象使用 `header.seq`，collection 使用 `values[0]`，map
使用 `attrs.key`。标量单元格使用 DataBind 的规范文本表示，包括精确的
有符号/无符号整数十进制文本、`true`/`false`，以及现有的 UUID、时间、
decimal、bigint 和 money 格式。

```c
char *csv = NULL;
size_t csv_len = 0;

if (data_bind_object_serialize_csv(order, &csv, &csv_len, &err) == DATA_BIND_OK) {
  send_csv(csv, csv_len);
}
data_bind_serialized_free(csv);
```

CSV 无法用单行无损表示空集合或映射。遇到这些值、`NULL`、超过
255 字节的路径、非 UTF-8 文本/字节、包含 NUL 的字节，或者包含 `.` 或 `[`
的 map key 时，会返回 `DATA_BIND_ERR_TYPE_MISMATCH`；序列化器绝不会丢弃
值或输出不完整的行。

## 二进制输出

提供包含匹配 schema 的 codec 后，可以把 `DataBindObject` 重新编码为动态
TBE wire 格式：

```c
DataBindObject *order = NULL;
uint8_t *wire = NULL;
size_t wire_len = 0;

if (data_bind_object_from_json(codec, "Order", json, strlen(json),
                               &order, &err) == DATA_BIND_OK &&
    data_bind_object_serialize_bin(codec, order, &wire, &wire_len,
                                   &err) == DATA_BIND_OK) {
  send_payload(wire, wire_len);
}

data_bind_binary_free(wire);
data_bind_object_free(order);
```

当 transport 拥有输出 buffer 时，使用 `data_bind_object_serialize_bin_into`。
它会在写入前校验并测量完整对象。buffer 过小时返回
`DATA_BIND_ERR_INVALID_ARG`，保持 buffer 不变，并通过 `out_len` 报告所需
大小。

动态二进制 writer 与当前 `data_bind_parse` 布局匹配：支持小端 schema 中
的标量字段、enum、bool、UUID、定长/变长 bytes、string、定长/变长 list、
set、字符串键映射、定长复合类型和重复组。可选字段存在位图、union、
大端 schema 和仅文本扩展标量没有兼容的动态二进制解析
路径，会返回 `DATA_BIND_ERR_SCHEMA`。

## 记录流式处理

记录回调在不替换最终值 API 的前提下提供低延迟消费：

```c
static DataBindRecordAction on_order(void *ctx,
                                     const DataBindValue *record,
                                     uint64_t index) {
  consume_order(ctx, record, index); /* record 为借用值 */
  return DATA_BIND_RECORD_CONTINUE;
}

DataBindValue *all_orders = NULL;
data_bind_stream_t *stream = data_bind_stream_csv_path_create(
    codec, "Order", "side == \"Sell\"", &all_orders, &error);
data_bind_stream_set_record_callback(stream, on_order, app);

/* 文件、socket、HTTP 或 broker 代码提供数据块。 */
data_bind_stream_feed(stream, chunk, chunk_len);
data_bind_stream_finish(stream);
```

回调在调用 `feed` 或 `finish` 的线程中运行；DataBind 不拥有线程、事件循环、
网络连接或重试策略。背压由数据源实施：回调消费者准备好之前，不要发起
下一次异步读取。当前 parser ABI 不报告数据块的部分消费量，因此不提供
`feed` 中途暂停/恢复。

CSV 和 XML 使用与 JSON 相同的 schema 绑定器：

- CSV 嵌套列使用 `header.seq`、`levels[0]`、`bids[0].price` 和 `attrs.x`
  等路径；TurboUtils 解析器适配器也接受 `header_seq` 等清理后的名称。
- CSV 顶层 scalar/enum/flags 值使用 `value` 列，绑定和校验均接受单列 CSV。
- CSV union 值根据每一行非空的 payload 列选择变体，因此合并后的动态
  表头可以在不同行表示不同变体。
- XML record 字段优先从同名子元素绑定，其次从属性绑定。
  `data_bind_parse_xml_path_all` 和 `data_bind_validate_xml_path` 接受
  XMLPath，用于选择重复 record 节点。

## 严格值访问

`data_bind_value_as_int()` 等便利访问器仍然可用，但它们会执行强制转换，
或在输入无效时返回零。新的第三方代码应优先使用严格访问器：

- `data_bind_value_get_int32`
- `data_bind_value_get_int64`
- `data_bind_value_get_double`
- `data_bind_value_get_bool`
- `data_bind_value_get_string`
- `data_bind_value_get_bytes`
- `data_bind_value_get_uuid`
- `data_bind_value_get_datetime`
- `data_bind_value_get_date`
- `data_bind_value_get_time`
- `data_bind_value_get_duration_milliseconds`
- `data_bind_value_get_decimal`
- `data_bind_value_get_bigint`
- `data_bind_value_get_money`

只有请求的转换有效时，严格访问器才返回 `DATA_BIND_OK`。

Schema `bool` 值绑定为 `DATA_BIND_VALUE_BOOL`。数字便利访问器仍可将其读取为
`0` 或 `1`，但严格类型检查应使用 `data_bind_value_get_bool`。

Schema `uuid` 值绑定为 `DATA_BIND_VALUE_UUID`，内部存储为 `turbo_uuid_t`。
二进制解析读取固定的 16 字节字段 payload。JSON、YAML、CSV 和 XML 绑定
接受 `01890f3e-5c5a-7cc2-9f2b-8b7f47f0c001` 等规范 UUID 文本；使用目标
为 `turbo_uuid_t.bytes` 的 `data_bind_value_get_uuid`，或使用
`data_bind_value_as_uuid_string` 读取。

Schema `bytes` 值绑定为 `DATA_BIND_VALUE_BYTES`。二进制解析读取定长或变长
bytes payload；JSON/XML 文本绑定读取字符串内容；CSV 绑定将单元格文本
作为 bytes 读取。使用 `data_bind_value_get_bytes` 或
`data_bind_value_as_bytes` 取得借用字节视图。

Schema `datetime` 值在 JSON、CSV 和 XML 文本绑定中绑定为
`DATA_BIND_VALUE_DATETIME`。接受的文本直接通过 TurboNet::Parser 解析。
调用方可以使用 `data_bind_value_get_datetime` 读取原生
`turbo_datetime_t`，或使用 `data_bind_value_as_datetime_string` 格式化。
二进制解析不定义隐式 datetime wire 格式；二进制时间戳应使用显式数字
schema 字段。

Schema `date`、`time` 和 `duration` 值在 JSON、CSV 和 XML 文本绑定中分别
绑定为 `DATA_BIND_VALUE_DATE`、`DATA_BIND_VALUE_TIME` 和
`DATA_BIND_VALUE_DURATION`。`date` 接受 `YYYY-MM-DD`、`YYYY/MM/DD` 或可
提取日期的 datetime 文本；`time` 接受 `HH:MM`、`HH:MM:SS` 或
`HH:MM:SS.mmm`；`duration` 接受 `1h30m5s250ms` 等单位文本和输出的
`H:MM:SS.mmm` 文本。使用对应的严格访问器或字符串格式辅助函数读取。
二进制解析不为这些时间标量定义隐式 wire 格式。

Schema `decimal` 值在 JSON、CSV 和 XML 文本绑定中绑定为
`DATA_BIND_VALUE_DECIMAL`。公共表示为 `DataBindDecimal { int64_t mantissa;
int32_t scale; }`，其中 `123.45` 存储为 `mantissa=12345`、`scale=2`。解析
会规范化小数末尾的零，`data_bind_value_as_decimal_string` 输出规范化的
十进制文本。Schema JSON 输出使用字符串，避免 decimal 精度被迫经过二进制
浮点表示。二进制解析不定义隐式 decimal wire 格式。

Schema `bigint` 值在 JSON、CSV 和 XML 文本绑定中绑定为
`DATA_BIND_VALUE_BIGINT`。公共表示是拥有所有权的规范十进制字符串，因此
大于 `int64` 的值仍保持精确。JSON 字符串输入是全精度路径；只有处于安全
整数范围内时才接受 JSON 数字输入。使用 `data_bind_value_get_bigint` 或
`data_bind_value_as_bigint_string` 读取借用文本。

Schema `money` 值在 JSON、CSV 和 XML 文本绑定中绑定为
`DATA_BIND_VALUE_MONEY`。公共表示为 `DataBindMoney { DataBindDecimal amount;
char currency[4]; }`。文本接受 `USD 123.45` 和 `123.45 USD`；JSON 还接受
`{ "amount": "123.45", "currency": "USD" }`。使用
`data_bind_value_get_money` 或 `data_bind_value_as_money_string` 读取。

字符串字段可以通过字段属性声明校验格式：

```tbe
message Endpoint {
  [format(ipaddr)] string ip;
  [format(url)] string href;
  [format(email)] string owner;
}
```

format 会在绑定和校验期间检查 JSON、CSV、XML 和默认文本，但绑定后的值
仍为 `DATA_BIND_VALUE_STRING`。支持的格式包括 `ipaddr`、`ip`、`cidr`、
`hostname`、`domain`、`email`、`url`、`uri`、`macaddr`、`mac`、`semver`、
`hex`、`base64`、`base64url`、`currency`、`json_pointer`、`jsonpath`、
`xpath`、`cron`、`color`、`mime` 和 `regex`。Regex 格式校验使用内置的
tiny-regex 实现。未知格式会被忽略，使外部 schema 注解可以与 DataBind
共存。

## 模式定义反射

反射输出是调用方拥有的结构体。应使用提供的宏初始化，使库能够跨 ABI
版本识别结构体大小：

```c
DataBindSchemaType type = DATA_BIND_SCHEMA_TYPE_INIT;
if (data_bind_schema_find_type(codec, "Order", &type)) {
  printf("%s has %zu fields\n", type.name, type.field_count);
}

DataBindSchemaField field = DATA_BIND_SCHEMA_FIELD_INIT;
if (data_bind_schema_field_at(codec, "Order", 0, &field)) {
  printf("%s: %s\n", field.name, field.kind);
  if (field.format) printf(" format=%s\n", field.format);
}
```

TurboUtils 解析器模块将 `schema.types`、`schema.fields`、
`schema.type_exists`、`schema.enums`、`schema.flags` 和 `schema.unions` 委托给
这些反射 API。解析器本地的 schema 属性/布局辅助函数目前仍位于此 ABI
之外，因为它们保留解析器特有的布局摘要。

## MIR 输出

`data_bind_generate_mir` 通过回调写出数据，避免跨 ABI 暴露 `FILE*`：

```c
static int write_cb(const void *data, size_t len, void *user) {
  FILE *out = (FILE *)user;
  return fwrite(data, 1, len, out) == len ? 0 : -1;
}

DataBindError err = DATA_BIND_ERROR_INIT;
DataBindStatus status =
  data_bind_generate_mir("orders.tbe", write_cb, stdout, 0, &err);
```

传入 `binary_output = 1` 可生成 BMIR。主程序随后可以跳过 parser 生成，
直接根据任一产物创建普通 codec：

```text
构建/部署阶段：
  packet.tbe -> data_bind_generate_mir(..., binary_output = 1) -> packet.bmir

运行阶段：
  精确的 packet.tbe 字节 + packet.bmir -> data_bind_create_from_bmir() -> DataBind codec
  DataBind codec + 类型名 + wire 字节 -> data_bind_object_from_bin() -> DataBindObject
  DataBindObject -> 字段访问或 JSON/YAML/XML/CSV/二进制序列化
```

```c
DataBind *codec = NULL;
DataBindObject *order = NULL;

/* schema_text 必须包含生成 BMIR 时使用的精确字节。 */
DataBindStatus status = data_bind_create_from_bmir(
    schema_text, schema_len, bmir_data, bmir_len, &codec, &err);
if (status == DATA_BIND_OK) {
  status = data_bind_object_from_bin(
      codec, "Order", wire_data, wire_len, &order, &err);
}

const char *name = NULL;
size_t name_len = 0;
if (status == DATA_BIND_OK) {
  const DataBindValue *root = data_bind_object_value(order);
  const DataBindValue *name_value = data_bind_value_get(root, "name");
  status = data_bind_value_get_string(name_value, &name, &name_len);
}

char *json = NULL;
if (status == DATA_BIND_OK) {
  status = data_bind_object_serialize_json(order, &json, NULL, &err);
}

data_bind_serialized_free(json);
data_bind_object_free(order);
data_bind_free(codec);
```

文本 MIR 使用 `data_bind_create_from_mir()`。生成产物内嵌 parser ABI 版本和
源 schema 字节的 FNV-1a 指纹；产物与 schema 不匹配时加载失败。加载路径
省去了 schema 到 MIR 的生成步骤，不需要 C 编译器，但 MIR 仍会通过 JIT
把模块链接为原生代码。Schema 字节必须精确匹配，包括 LF 与 CRLF 的换行
差异。Schema 和 MIR/BMIR 产物都属于可信代码输入。

完整可运行示例位于
[`examples/bmir_runtime.c`](examples/bmir_runtime.c)，对应 schema 位于
[`examples/packet.tbe`](examples/packet.tbe)。示例将生成和运行时加载放在
不同函数中，生产应用可据此在不同进程或部署阶段执行两部分操作。

使用仓库 preset 构建并运行：

```powershell
cmake --preset win-release-user
cmake --build --preset win-release-user --target data_bind_bmir_example
Push-Location build\Msvc-Release\bin
.\data_bind_bmir_example.exe
Pop-Location
```

预期输出（字段标签与示例程序的实际标准输出一致）：

```text
generated: packet.bmir
object.id: 42
object.name: Turbo
json: {"id":42,"name":"Turbo"}
```

使用已安装 TurboUtils 包的程序应链接导入 target：

```cmake
find_package(TurboUtils CONFIG REQUIRED)
target_link_libraries(my_runtime PRIVATE TurboUtils::DataBind)
```

## 运行时生成与预生成 BMIR 的选择

两条路径都不需要 C 编译器，并且都会产生普通 `DataBind` codec。它们的主要
差异在 codec 创建和部署阶段，而不是创建后使用的 `DataBindObject` API。

| 决策项 | 运行时生成 | 预生成 BMIR |
|---|---|---|
| 入口 | `data_bind_create_from_text()` | `data_bind_create_from_bmir()` |
| 部署输入 | 可信模式定义文本 | 精确的可信模式定义字节和匹配的 BMIR |
| 首次创建 | 解析模式定义、生成 MIR，然后 JIT 链接 | 解析模式定义、校验/读取 BMIR，然后 JIT 链接 |
| 同一进程内重复创建 | 可复用现有模式定义哈希 MIR 缓存 | 当前每次创建编解码器都会读取并 JIT 链接 BMIR |
| 模式定义更新 | 替换模式定义文本 | 重新生成并部署模式定义/BMIR 配对 |
| 加载后的对象 API | `DataBindObject` 和所有支持的序列化器 | 相同的 `DataBindObject` 和序列化器 |
| 最适用场景 | 开发、工具和简单部署 | 模式定义稳定且编解码器冷启动成本重要的部署 |

当前实现事实：

- 预生成 BMIR 省去 schema 到 MIR 的生成，但不会省去 schema 解析、产物校验
  或 MIR JIT 链接。
- 运行时生成的 codec 参与现有 schema-hash MIR cache。
  `data_bind_create_from_mir()` 和 `data_bind_create_from_bmir()` 当前不会把
  加载的模块插入该 cache。
- codec 加载后，两条路径都暴露相同的动态 `parse_<Type>` ABI，以及格式
  无关的对象/序列化器边界。

仓库目前没有比较两条路径 codec 创建延迟或稳态解析吞吐量的 benchmark。
因此，BMIR 跳过 MIR 生成后预期具有较低冷启动延迟，但这不是经过测量的
保证。若仅为性能选择 BMIR，应先测量具有代表性的 schema 和部署环境。

默认建议：

- 重视运维简单性时，先使用 `data_bind_create_from_text()`。
- Schema 稳定，而且实测启动成本足以证明管理额外版本化产物的合理性时，
  使用预生成 BMIR。
- 无论使用哪种模式，都应在初始化阶段创建 codec，并在多条消息间复用，
  不要每条消息创建一个 codec。

## 版本检查

`DATA_BIND_VERSION` 是编译时头文件版本。运行时检查方式如下：

```c
if (data_bind_abi_version() != DATA_BIND_ABI_VERSION) {
  /* 头文件与库的 ABI 不匹配。 */
}
```

`data_bind_library_version()` 返回 `major * 10000 + minor * 100 + patch`。
`data_bind_version_string()` 返回诊断字符串。

## MIR 模块缓存

运行时生成路径按 schema hash 缓存 JIT 编译后的 MIR 模块。根据相同 schema
创建的多个 codec 可以共享同一个已编译解析器，从而降低内存占用和 codec
创建时间。MIR/BMIR 产物加载器当前不使用此缓存。

### 缓存行为

- **自动启用**：默认启用缓存，无需修改代码。
- **基于 hash**：Schema 使用 FNV-1a 计算 hash。相同 schema 文本会生成
  相同解析器，不受文件路径或创建顺序影响。
- **引用计数**：只要仍有 codec 引用缓存模块，该模块就保留在内存中。
  使用某个缓存模块的最后一个 codec 释放后，该模块即可清理。
- **线程约束**：缓存状态为进程全局状态，内部没有同步。调用方可能并发
  执行 codec 创建或缓存控制操作时，应串行化这些操作或禁用缓存。

### 缓存控制

```c
/* 为当前进程禁用 cache。 */
data_bind_set_cache_enabled(0);

/* 重新启用 cache。 */
data_bind_set_cache_enabled(1);

/* 清理不再被引用的缓存模块。 */
data_bind_clear_cache();
```

### 性能影响

典型 codec 创建时间（示例 schema 包含 10 个 message 类型）：

| 场景 | 无缓存 | 命中缓存 | 加速比 |
|----------|---------------|------------------|---------|
| 首次创建 | ~50ms | ~50ms | 1x |
| 后续创建 | 每次 ~50ms | 每次 ~0.5ms | 100x |

缓存命中率取决于 schema 复用模式。重复根据相同 schema 定义创建 codec 的
应用，例如每个请求实例化 codec，能从缓存中获得最大收益。

## 值对象池

DataBind 为 `DataBindValue` 结构维护一个最多容纳 64 个节点的小型对象池，
用于降低解析过程中的分配开销。它对深度嵌套的 JSON/CSV/XML 文档尤其有利。

### 对象池行为

- **自动启用**：默认启用对象池。
- **大小限制**：对象池最多保留 64 个空闲节点，额外释放的节点会返回系统
  allocator。
- **线程安全**：对象池状态是进程全局状态。分配和释放使用有界原子 slot
  操作，热路径中没有 mutex。
- **复用**：对象池节点会被清空并复用，避免重复调用 malloc/free。

### 对象池控制

```c
/* 禁用对象池，所有分配恢复使用 malloc/free。 */
data_bind_set_value_pool_enabled(0);

/* 重新启用对象池。 */
data_bind_set_value_pool_enabled(1);

/* 查看对象池统计信息。 */
size_t allocated, reused;
data_bind_get_value_pool_stats(&allocated, &reused);
printf("对象池效率：复用 %zu / 分配 %zu = %.1f%%\n",
       reused, allocated, 100.0 * reused / allocated);
```

### 性能影响

固定的进程全局对象池最多保留 64 个空闲节点，从而避免反复分配节点。节点
通过 atomic exchange 获取，通过 atomic compare-exchange 归还；每次操作
最多检查 64 个 slot。禁用对象池时，控制边界以原子方式关闭 slot，然后
释放缓存节点。发生并发 bitmap CAS 冲突时，复用会无等待地暂停；调用
`data_bind_set_value_pool_enabled(1)` 可恢复复用。

应使用 `benchmark_data_bind_pool` 在本地进行单线程和 1/2/4/8 线程测量，
不要假定不同工作负载都能获得固定百分比的性能提升。

### 为什么返回普通对象？

DataBind 有意返回普通对象，原因如下：

1. **性能**：普通对象在纯数据访问模式下没有方法查找开销。
2. **通用性**：与其他语言的序列化库一致，例如 Protobuf、Jackson、
   MessagePack 和 `json.loads()`。
3. **灵活性**：调用方可以自行选择把数据包装为类，或按普通数据处理。
4. **内存效率**：纯数据结构不需要方法分发表或 vtable。

对于数据密集型应用，例如金融分析、日志处理和 ETL 流水线，普通对象可
提供最佳吞吐量。对于业务逻辑密集型应用，例如用户管理和工作流系统，可在
需要行为的位置额外包装为类。
