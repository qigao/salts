# CMeta 有限 DFA 推导与 CFlow Admission 设计

**状态：** Accepted for implementation

**分支：** `feat/cmeta-finite-compute`

**基线：** `origin/master` + finite compute commit `8a1e110`

**日期：** 2026-08-24

## 1. 目标

在保持合法 C11/CMeta 语法、不引入独立 DSL、不增加 executor 热路径推导的前提下，实现：

- CMeta 宏行表到运行期有限关系的投影；
- 使用调用方提供的有界 workspace，把有限关系构造成确定性 trie/DFA；
- 对 1–3 个符号组成的输入序列进行有界推导；
- 在构建阶段拒绝重复输入与冲突输入；
- CFlow 在 plan compile 阶段通过 DFA 推导 plan opcode，executor 继续消费预解码 instruction；
- Lean 增加显式 DFA 的执行语义和有限关系一致性性质。

## 2. 非目标

- 不解析任意 C 预处理器语义；
- 不在运行期生成新的 C 类型；
- 不增加 `.cmeta` 文件或第二套语法；
- 不把 re2c、Lemon 或 TurboParser 链接进 TurboUtils/CMeta；
- 不在逐元素 executor 热路径执行 DFA；
- 不构造通用字节码 VM。

文本关键字到 symbol id 的 re2c 投影留作独立的 build-time 可选层；本阶段输入已经是稳定整数 symbol。

## 3. 单一事实源

规则保持为普通宏行列表：

```c
#define CFLOW_PLAN_INFERENCE_ROWS \
    (CFLOW_OP_FILTER, CFLOW_OUTPUT_SAME, CMETA_CARD_FILTER, CMETA_PLAN_FILTER), \
    (CFLOW_OP_MAP, CFLOW_OUTPUT_RETURN, CMETA_CARD_ONE, CMETA_PLAN_MAP)
```

同一行列表产生两种投影：

```c
ValueFunction3(CFlowPlanOpcode, CFLOW_PLAN_INFERENCE_ROWS);
InferenceRules3(cflow_plan_rules, CFLOW_PLAN_INFERENCE_ROWS);
```

`ValueFunction3` 是编译期 token/value 投影；`InferenceRules3` 是 C DFA 的运行期 relation。不得维护第二份手写 switch。

## 4. CMeta API

新增 `<cmeta/infer.h>`：

```c
typedef uint32_t cmeta_infer_symbol;
typedef uint32_t cmeta_infer_value;

typedef struct cmeta_infer_rule {
    cmeta_infer_symbol symbols[CMETA_INFER_MAX_ARITY];
    cmeta_infer_value result;
} cmeta_infer_rule;

typedef struct cmeta_infer_relation {
    const cmeta_infer_rule *rules;
    size_t rule_count;
    size_t arity;
} cmeta_infer_relation;
```

公开操作：

```c
void cmeta_infer_dfa_init(...);
cmeta_infer_status cmeta_infer_dfa_build(...);
cmeta_infer_status cmeta_infer_dfa_eval(...);
const char *cmeta_infer_status_string(cmeta_infer_status status);
```

调用方拥有 state/transition 数组。容量上界为：

- states：`1 + rule_count * arity`
- transitions：`rule_count * arity`

输入 arity 限制为 1–3。构建后 DFA 只读，可由多个只读查询者并发使用；重新 build 属于控制面，要求无并发查询。

## 5. 算法

构建阶段逐行插入 trie：

1. 从 state 0 开始；
2. 对每个 symbol 查找已有 `(state, symbol)` 转移；
3. 缺失时分配一个新 state 和 transition；
4. 到达末状态时写入 accept result；
5. 已接受的末状态再次出现时，等值结果返回 DUPLICATE_RULE，不同结果返回 AMBIGUOUS_RULE；
6. 构建完成后按 `(from, symbol)` 排序转移。

构建时间 `O(R² × k)` 的保守上界来自小型线性插入查找，`R` 为规则数、`k <= 3`。推导用二分查找，时间 `O(k log T)`，其中 `T <= Rk`。本阶段服务于 admission，不宣称逐值热路径收益。

## 6. 状态、所有权与失败语义

- relation 借用静态宏投影的规则数组；
- DFA 借用调用方 state/transition workspace；
- CFlow plan compile 在栈上拥有 workspace；
- compiled plan 只保存推导后的 opcode、类型和 handler，不借用 DFA；
- evaluator 成功时才写 `out_result`。

错误必须区分：

- `INVALID_ARGUMENT`：空指针、非法 arity、未构建 DFA 或输入长度错误；
- `CAPACITY_EXCEEDED`：caller workspace 不足；
- `DUPLICATE_RULE`：同一输入重复声明同一结果；
- `AMBIGUOUS_RULE`：同一输入声明不同结果；
- `NO_RULE`：查询序列不在有限关系中。

失败不 fallback、不返回默认 result。

## 7. CFlow 集成

`CFlowPlanInferenceRows` 描述 direct-plan 支持的：

```text
(op, output_rule, cardinality) -> plan_opcode
```

`cflow_plan_compile` 和 capability query 在控制面构造一次局部 DFA，并用它替代 `opcode_for` switch。既有 callable bind、类型相等检查、handler predecode 和执行路径不变。

`cflow_plan_compile_stats.inference_queries` 记录成功编译出的非 source instruction 数，便于测试确认 admission 推导发生；它不统计 executor 数据项。

## 8. Lean 边界

Lean 增加显式 DFA：

- transition relation：`(state, symbol) -> state`；
- accept relation：`state -> result`；
- `run` 对 symbol list 求值；
- 成功 step 必须来自 transition relation；
- 成功 accept 必须来自 accept relation；
- 测试覆盖 accepted、missing、ambiguous transition relation。

Lean 不证明 MSVC/GCC 预处理器正确，也不声称自动验证生成的 C 对象码。C 侧对全部声明规则做穷举等价测试。

## 9. 兼容性与验证

- 公开 API 仅新增；既有枚举值、结构字段顺序和执行结果不改变；
- `cflow_plan_compile_stats` 尾部新增字段，静态库消费者重新编译即可；
- CMeta 新 API 同时编译 C11 与 C++17 public header；
- 最小验证：CMeta inference tests、CMeta C++ header test、CFlow pipeline tests、Lean focused tests；
- 相邻回归：全部 CMeta/CFlow CTest；
- 最终验证：release build、CTest、`lake test`、`lake build`、proof-escape 扫描、`git diff --check`。

## 10. 风险

- **HIGH**：静态 ValueFunction 与运行 DFA 使用不同规则源会漂移；必须共享行列表。
- **HIGH**：DFA 进入逐元素 executor 会制造性能回归；只允许 plan compile/admission 使用。
- **HIGH**：查询失败返回默认 opcode 会执行错误 handler；必须 fail fast。
- **MED**：调用方容量计算溢出；公共容量宏只适合编译期小常量，运行期容量由调用方先 checked arithmetic。
- **MED**：大规则集的线性构建会变慢；达到实测瓶颈后再引入生成器或排序批构建。
- **LOW**：一元分类使用 DFA 比 switch 更重；价值在统一规则/验证模型，不宣称单次查询更快。
