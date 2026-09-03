# CFlow Stream `take` / `skip` 实现计划

1. 先添加行为测试，覆盖边界、顺序、重复执行、解释/编译 parity 和 Source 短路，确认缺少 API 时测试构建失败。
2. 向 Graph IR 追加 TAKE/SKIP opcode 与不可变 limit 参数；更新 clone、lower、optimize、validate、结构相等和 effect/property 推导。
3. 为 Stream 添加 fluent 方法，保持现有 callable operator 生成路径不变。
4. 在 Run 中增加每节点独立计数器，实现 managed-value 安全的丢弃与 `take` 上游短路。
5. 在 direct plan 中增加 materialized TAKE/SKIP 指令，禁用不安全的 fused/parallel 路径，并同步 certificate fingerprint/rows。
6. 更新 Container 文档、C++ header test、managed Stream test 与 install consumer。
7. 依次运行最小测试、CFlow/Container 相邻回归、Release、ASan 和安装消费验证。
