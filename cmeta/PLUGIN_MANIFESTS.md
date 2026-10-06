# 静态 Plugin/service declaration（#926）

`<cmeta/plugin.h>` 用 immutable role rows 声明一组 canonical interface 的 provides/requires 成员关系。它属于 `Salts::CMeta` core，不依赖 `Salts::Plugin`。声明不复制 methods、FunctionAbi、traits 或 lifecycle schema，不存储 interface handle、callback、模块状态或 lease，也不执行 constructor 或注册。

```c
#include <cmeta/manifest_view.h>
#include <cmeta/fingerprint.h>
#define DATABASE_METHODS(X,I) \
    X(I,F0,int,version,value,&cmeta_type_int,CMETA_ABI_SCALAR)
#define TLS_METHODS(X,I) \
    X(I,F0,bool,ready,value,&cmeta_type_bool,CMETA_ABI_SCALAR)
CMETA_INTERFACE(DatabaseDriver, DATABASE_METHODS);
CMETA_INTERFACE(TLS, TLS_METHODS);
cmeta_plugin(PostgresDriver,
    cmeta_provides(DatabaseDriver)
    cmeta_requires(TLS));
cmeta_registry(drivers,
    cmeta_manifest_plugin_entry("postgres", cmeta_plugin_meta(PostgresDriver)));
int main(void) {
    const cmeta_manifest_limits views = {CMETA_MANIFEST_DEFAULT_ITEMS,
        CMETA_MANIFEST_DEFAULT_DEPTH, CMETA_MANIFEST_DEFAULT_NODES};
    const cmeta_fingerprint_limits limits = {CMETA_FINGERPRINT_DEFAULT_DEPTH,
        CMETA_FINGERPRINT_DEFAULT_NODES, CMETA_FINGERPRINT_DEFAULT_ROWS,
        CMETA_FINGERPRINT_DEFAULT_STRING_BYTES};
    const cmeta_plugin_desc *provider = NULL;
    cmeta_status status = cmeta_manifest_get_plugin(&drivers, 0u, &views, &provider);
    if (status != CMETA_OK) return 1;
    const cmeta_interface_desc *required = NULL;
    status = cmeta_plugin_get_capability(provider, 1u, CMETA_PLUGIN_REQUIRES,
        &views, &required);
    if (status != CMETA_OK || required != &TLS_interface_meta) return 1;
    uint64_t fingerprint;
    return cmeta_contract_fingerprint_plugin(provider, &limits, &fingerprint) == CMETA_OK ? 0 : 1;
}
```

DSL 支持非空的 1..16 个 role row，通过既有 `CMETA_PP_FOR_EACH` 展开；`cmeta_plugin_meta(Name)` 直接引用同一 TU 的 canonical static descriptor。C11 `_Generic` / C++ constexpr pointer conversion 拒绝错误 interface descriptor 类型。不同 TU/DSO 的地址可以不同；不能用地址作为 capability 身份。

## 格式与查询

`cmeta_plugin_desc` 是 declaration format 1：exact `size`、`format_version`、诊断 name、borrowed capabilities 数组及 count。每行只有固定 role（PROVIDES=1 / REQUIRES=2）与 canonical interface 指针。Declaration name 不是运行期 `plugin_id`。手工构造的空集合有效；重复 row、同一 capability 的 provides/requires 和声明顺序均保留，不隐式合并或推断依赖已满足。

`cmeta_manifest_plugin_entry` 在既有 `CMETA_MANIFEST_PLUGIN=1` 下定义 typed descriptor 契约；此前没有该 kind 的 typed getter。既有 `CMETA_MANIFEST_CAPABILITY=2` 仍借用 `cmeta_interface_desc`，格式与语义不变。`cmeta_manifest_get_plugin` 校验 outer manifest 与完整 declaration；`cmeta_plugin_get_capability` 校验完整 declaration、index 与 exact role 后借用原 interface，不生成 handle。

查询参数分别为 manifest/declaration、index、显式 `cmeta_manifest_limits` 与输出指针。Capability query 另接收 expected role。成功返回 `CMETA_OK`；错误保留原输出：无效 size/name/pointer/role/index 或 limits 为 `CMETA_INVALID_ARGUMENT`，kind/format/role 不匹配为 `CMETA_TYPE_MISMATCH`，row 或 identity 预算耗尽为 `CMETA_CAPACITY_EXCEEDED`。Views 的 `max_items` 是每个表的上限，identity budget 跨所有 capability 共享；沿用既有 interface validator，legacy ABI-only rows 可发现，但完整 fingerprint 不接受它们。

指纹查询使用 [`FINGERPRINTS.md`](FINGERPRINTS.md) 的累计 node/row/string/depth 预算，直接串联 canonical interface row。Domain 6 的 v1 body 是 capability count，再逐行编码 role 与 Interface row；不重复子 envelope，不包含 declaration 的诊断 name。原 domains 1..5 的输出不变。

## 状态归属、错误与兼容性

Declaration 是显式 discovery set，不声明 runtime exports 的全集，不授予执行或所有权。`Salts::Plugin` 的 `export_id`、`contract_id`、`contract_version`、capability bits、ABI 4 admission、start/stop 和 lease 继续由 Plugin owner 管理。应用显式选择 provider，再用现有 `salts_plugin_export_require_interface` 验证 domain ID/version/capabilities 和 canonical descriptor。CMeta 不选择 provider、不加载模块、不推进生命周期，也不实现第二个依赖 resolver 或 ownership registry。

若应用需要跨模块消费 declaration，可像正式 fixture 一样通过现有 FUNCTION export 发布 exact getter：它返回 BORROWED 的 `const cmeta_manifest *`，并具有 canonical FunctionAbi。Provider 的 C/C++ static table 无需 constructor；原 Plugin query 仍只接受 ABI 4。Host 在持有原 lease 时查找并校验 getter contract、执行确切 bridge、查询 declaration、校验所选 provider，最后释放 lease。一个 lease 覆盖 descriptor、interface handle、callback 和指纹读取的整个借用期；复制 descriptor 或返回指针不会延长该期。

兼容性（MED）：新 public metadata/query 是 additive；Plugin ABI 4、Reflection ABI 3、generic manifest v1 布局和所有旧 kind 编号均不变。跨 DSO 仍须在读取 metadata 前协商这些契约。Plugin fingerprint 是 ordered membership/interface **形状**，不包含 runtime domain ID/version 或 native implementation；相同形状的两个模块可以同 hash。Consumer 必须另行验证 Plugin 的 ID/version，并对 TypeDesc 未表达的 Struct/enum schema 显式组合相应指纹。摘要不能替代 canonical compatibility 或安全认证。

失败没有半更新：query/hash 保持原输出，provider admission、dependency failure、retry 和停止/卸载收场由原 owner 处理。Provider release/unload 后禁止继续读取 declaration；完整的派生 `uint64_t` 指纹可继续保存。

## 正式验证

- `cmeta_plugin_test` / `cmeta_plugin_cpp_test`：direct canonical linkage、typed discovery、roles/order、跨 TU、budget、失败输出和固定向量；错误类型有 C/C++ compile-fail。
- `salts_plugin_manifest_test`：真实 C/C++ metadata 模块，在 ABI 4 和 exact getter contract 校验后读取同一 schema；两个模块均得到 domain 6 固定向量 `5aa1c2c29cbe9f20`。该向量由 `FingerprintService` 的 [`FINGERPRINTS.md`](FINGERPRINTS.md) interface row、PROVIDES/REQUIRES 两行独立计算。
- 同一集成测试检查 active lease 仍为 1、callbacks_inflight 为 0、状态仍 STARTED；request_stop 后 lease 阻止卸载，释放后可卸载。Required capability 通过显式选定模块的既有 Plugin contract API 校验，缺失 export 或错误 version 明确失败。
- GCC、GCC ASan、MSVC、ClangCL、AppleClang CI 运行上述用例；原 Plugin loader/lifecycle/contract 回归继续运行。正式 installed Reflection C/C++ tests 只链接 `Salts::CMeta` 与 TinyTest，实际发现、查询并 fingerprint declaration。

没有新增 linker aggregation backend；portable static array 仍是唯一参考实现。ELF/Mach-O/COFF section syntax、constructor registration、package pinning 和 mutable CMeta registry 均未加入。
