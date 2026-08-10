# 编译型 JSONPath Program

## 背景

原 JSONPath 查询在每次 `json_path_query()` 调用中解析表达式，构造由
`down`/`sibling` 指针连接的 AST，递归执行后立即释放。重复查询会重复承担词法、
语法、小对象分配、字符串复制和 AST 指针追逐成本。

JSON 文档仍以树为事实源。大对象成员拥有可重建的连续顺序索引和 key hash
索引；JSONPath program 只保存查询逻辑，不保存任何 JSON 节点指针。

## 方案比较

1. 保留 AST 并缓存：迁移成本低，但继续保留分散分配和指针追逐，且 program
   布局不连续。
2. 完整栈式 VM：可把所有布尔表达式降为跳转字节码，但一次迁移会同时改变
   parser、表达式求值和路径遍历，回归面较大。
3. 连续指令数组与索引边：解析阶段仍使用现有 AST，随后将可达节点一次性降为
   固定大小指令；`down`/`sibling` 改为数组索引，字符串进入连续常量池。

当前选择方案 3。它移除了执行期 AST 所有权和指针关系，同时完整复用现有
JSONPath 语义。后续若 profiling 证明过滤表达式仍是热点，可以在不改变公开
program API 的前提下，将表达式子图继续降为栈式跳转指令。

## 数据与生命周期协议

- `json_path_compile()` 复制表达式中的常量并返回唯一拥有的 program。
- program 由连续指令数组和连续常量池组成，不引用输入表达式或 JSON 树。
- `json_path_get_compiled()` 和 `json_path_query_compiled()` 在调用期间借用 program
  与 JSON 根；返回的 JSON 节点继续由 JSON 根拥有。
- query result 拥有结果指针数组，但不拥有匹配节点。
- `json_path_program_free()` 释放 program；调用方必须保证此时没有执行正在借用它。
- program 构造后不可变，但当前 JSONPath API 使用模块级错误字符串，因此线程
  拓扑仍定义为单线程或外部同步；JSON 树也不得在执行期间被并发修改。

编译最多接受 1,048,576 条可达指令和 64 MiB 常量数据。计数、乘法和累加均在
分配前检查；超过限制、语法错误或 OOM 均返回 `NULL` 并设置
`json_path_get_error()`。

## 执行路径

普通属性指令在编译期保存 key bytes、长度和 hash。执行时：

```text
program GET_KEY -> json_object_get_hashed_v()
                -> 大对象 hash index
                -> 小对象短链扫描
```

数组下标直接使用已有连续数组索引。wildcard、union、过滤和布尔短路关系使用
指令数组中的索引边执行，不再访问临时 AST。

## 兼容、迁移与回滚

原有 `json_path_get()` / `json_path_query()` 签名和结果语义不变，它们现在是
compile-execute-free 的兼容包装。重复执行方可迁移到 compile-once API；一次性
调用方无需修改。

TurboParser facade 提供对应 opaque program API，不向调用方暴露内部指令布局。
如需回滚，可删除新增 facade/program API，并让旧入口恢复直接 AST 执行；JSON
树、对象索引和序列化格式不需要迁移。

## 流式执行

`json_path_stream_create()` 在同一个 immutable program 上建立 SAX matcher，
不会创建 `json_value_t` 或 object hash index。当前 streamable 子集包括：

- root/key 路径；
- 非负数组下标；
- wildcard；
- key/index union。

匹配到的值通过 `on_match_start`、raw SAX value events 和
`on_match_end` 交付。字符串、object key 和数字 token 都是 callback 期间的
borrowed view，流式接口不会返回可跨 callback 保存的 `json_value_t *`。

filter、递归路径、负下标和依赖完整对象状态的表达式在创建 matcher 时明确
失败，并应改用 DOM API；不会隐式退化为构建完整 DOM。stream matcher 固定
限制 64 个 path segment、64 条 union alternative 和 256 层输入深度，超过
限制直接返回错误。

## 验证范围

- 简单属性、负数组下标、Unicode 属性名
- wildcard 多结果、对象 union
- 当前节点过滤、比较和布尔表达式
- program 脱离表达式缓冲区后复用，并跨多个 JSON 根执行
- 旧 API 与 TurboParser facade 兼容性
- stream wildcard、完整 subtree 事件、key/index union、任意 chunk 边界
- stream 对 filter、负下标和 callback 失败的 fail-fast 行为
- Release benchmark 以及 Debug/AddressSanitizer 测试
