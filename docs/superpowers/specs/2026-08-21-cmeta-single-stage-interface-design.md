# CMeta 单阶段 Interface DSL 设计

## 背景

CMeta 当前用三个宏表达接口协议：

```c
interface(counter, COUNTER_METHODS);
implements(counter, basic_counter, caps, ...);
interface_impl(counter, COUNTER_METHODS)
```

`interface(...)` 生成 handle、vtable 与函数声明；`interface_impl(...)` 在一个实现翻译单元中再次重放相同的 method schema，生成 wrapper、bind、能力查询和反射 metadata；`implements(...)` 则把一组具体函数绑定到 vtable。前两者要求调用方重复维护 `(interface, METHODS)`，后两者的命名又把“接口公共代码生成”和“具体实现绑定”都称为 implementation。

CMeta 已采用单阶段 `Enum`、`Struct` 和 typed-container 声明。Interface 继续保留声明/实现两阶段会增加示例、迁移和多 TU 使用成本。

## 决策

删除公开宏 `interface_impl` 与内部宏 `CMETA_INTERFACE_IMPL`，不提供 deprecated/no-op 兼容入口。

`interface(I, METHODS)` / `CMETA_INTERFACE(I, METHODS)` 改为 header-complete 声明，一次生成：

- interface handle 与 vtable 类型；
- typed wrapper；
- `I_bind`；
- `I_valid`、implementation/capability 查询；
- method metadata 与 `I_interface()`。

`implements(I, NAME, CAPS, ...)` / `CMETA_IMPLEMENTS(...)` 保持单一职责：生成具体 vtable 和 `NAME_as_I()` binder。

新调用形状为：

```c
interface(counter, COUNTER_METHODS);

implements(counter, basic_counter, COUNTER_CAP_RESET,
    .add = basic_add,
    .value = basic_value,
    .reset = basic_reset
);
```

## 生成与链接模型

Interface wrapper、bind/query 函数改为 `static inline`。反射数组和 descriptor 改为 header-local `static const`，与 CMeta `Enum`、`Struct` 和 typed container 的多 TU 模型一致。

同一接口在不同翻译单元中的 descriptor 地址可能不同。调用方只能依赖名称、方法列表、arity 等语义内容，不得把 `I_interface()` 返回地址当作进程级类型 ID。

Wrapper 仍直接调用 `self->vtable->method(self->self, ...)`，不新增校验、fallback、动态分配、表查找或错误转换。`I_valid`、capabilities 与 implementation 文本行为保持不变。

## 兼容性影响

这是有意的源码和链接接口清理：

- 使用 `interface_impl(...)` 或 `CMETA_INTERFACE_IMPL(...)` 的源码必须删除该行；
- 新编译消费者从头文件获得 inline wrapper，不再依赖 CFlow/CMeta 库中的接口 wrapper 外部符号；
- 已经编译、仍引用旧 wrapper 外部符号的对象需要重新编译；不承诺旧二进制兼容；
- `interface(...)`、`implements(...)`、handle/vtable 布局、method ABI、capability bits 和 metadata 内容保持稳定。

CMeta/CFlow 当前属于预案与测试阶段，用户已明确选择直接删除，不保留迁移 shim。

## 修改范围

1. `cmeta/include/cmeta/interface.h`
   - 把 wrapper、bind/query 和 metadata 生成合并到 `CMETA_INTERFACE`；
   - 生成函数使用 `static inline`；
   - 删除 `CMETA_INTERFACE_IMPL`、`interface_impl` alias 及两阶段说明。
2. `cflow/src/interfaces.c`、`cflow/src/scheduler.c`
   - 删除 `CMETA_INTERFACE_IMPL(...)` 调用。
3. `cflow/examples/demo_interface.c`、`demo_cmeta_standalone.c`
   - 删除 `interface_impl(...)` 调用并更新输出文案。
4. `cmeta/tests/cmeta_core_test.c`
   - 增加单阶段 interface + implements 回归测试，覆盖 wrapper、binder、valid、implementation、capabilities 和 metadata。
5. CMeta/CFlow 文档
   - 删除两阶段 API 描述，明确 header-local descriptor 的语义身份。

不新增目标、依赖、运行时状态或配置。

## 测试策略

按 TDD 顺序执行：

1. 先新增只使用 `interface + implements` 的 CMeta 回归测试；在旧实现下因缺少 wrapper 定义而链接失败，证明测试覆盖被删除的阶段。
2. 合并生成逻辑，运行 `cmeta_core_test`。
3. 删除 CFlow 的 `CMETA_INTERFACE_IMPL` 调用，构建并运行 `cflow_graph_test`、`cflow_pipeline_test`。
4. 编译两个 interface 示例，验证自然 DSL 不再需要 `interface_impl`。
5. 扫描源码，确认除迁移说明外不存在 `interface_impl` 或 `CMETA_INTERFACE_IMPL`。
6. 运行 MSVC Release 全量构建与 CTest；用 Clang C11 做 CMeta/CFlow 相关语法检查。
7. 比较接口相关头文件编译时间或至少核对生成代码，确认没有新增运行时分配和间接层；vtable 调用本身保持不变。

## 回滚

若 header-complete 生成在支持的编译器或多 TU 场景出现无法接受的冲突，可恢复 `CMETA_INTERFACE_IMPL` 的 extern 定义模型和四个 CFlow 实现调用点。回滚不会改变 vtable 布局、具体实现函数或运行时所有权，仅恢复两阶段代码生成与旧外部 wrapper 符号。
