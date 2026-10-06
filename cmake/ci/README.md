# Benchmark 报告

`native-io-benchmarks.yml` 通过 CTest 执行 benchmark 和既有 verifier。
详细 CSV、Markdown、日志保留在 `io-benchmark-*` artifact，保留 30 天；
成功 PR 的原始逐次采样及 syscall trace 仍按既有策略裁剪。Summary 不再追加数据表。

`render-benchmark-verdicts.ps1` 同时输出 `benchmark-verdicts.csv` 和
`benchmark-metrics.json`，图表与文字判定复用同一次计算。配对指标显示中位数与 MAD；
诊断项不作为性能门禁，缺失数据不算通过。

`benchmark-report.yml` 在 `Salts CI` 完成后处理 PR 报告：

1. 从默认分支读取报告代码，仅把本轮 artifact 作为数据输入。
2. 用 Ubuntu 的 `python3-matplotlib` 生成结果矩阵、性能误差图及合并后的 `report.json`。
3. 使用 `GITHUB_TOKEN` 自动创建或更新独立 `benchmark-reports` 分支，只存 PNG。
4. 在 PR 中创建或更新带固定标记的机器人评论，可见内容只有两张图片；点击图片打开 CI run。

图片引用不可变 commit URL，路径按 PR、run、attempt 隔离。已有图片不会被后续报告覆盖。
该方案用于当前公开仓库；图片分支长期保留，Actions artifact 的 30 天期限不适用于图片。
发布 job 需要 `actions: read`、`contents: write`、`pull-requests: write`，无需新增 Secret。
仓库或组织若禁止 Actions 写入，必须先允许这些权限；分支保护也需允许机器人更新报告分支。

新 workflow 必须进入默认分支后，`workflow_run` 才会触发。只发布仍对应 PR 当前 head 的结果，
已发布或更新的 run attempt 不重复发布。失败/取消仍可展示已收集的数据，并标出 job 状态；
整轮没有 benchmark artifact 时不评论。

本地使用下载的真实 CI evidence 生成预览：

```powershell
gh run download RUN_ID --repo qigao/salts --pattern 'io-benchmark-*' --dir benchmark-inputs
python cmake/ci/render-benchmark-report.py --input benchmark-inputs --output benchmark-report --head HEAD_SHA --run RUN_ID --conclusion success
node --test .github/scripts/publish-benchmark-report.test.cjs
python -m unittest discover -s cmake/ci/tests
```

将 `RUN_ID`、`HEAD_SHA`、`--conclusion` 替换为实际运行信息；本地 Python 需要 Matplotlib。
`benchmark-metrics.json` 必须来自采用新报告流程的运行，旧 artifact 缺少此文件时标记为 missing。
