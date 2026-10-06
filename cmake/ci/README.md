# Benchmark 报告

`native-io-benchmarks.yml` 通过 CTest 执行 benchmark 和既有 verifier。
详细 CSV、Markdown、日志保留在 `io-benchmark-*` artifact，保留 30 天；
成功 PR 的原始逐次采样及 syscall trace 仍按既有策略裁剪。Summary 不再追加数据表。

`render-benchmark-verdicts.ps1` 同时输出 `benchmark-verdicts.csv` 和
`benchmark-metrics.json`，图表与文字判定复用同一次计算。配对指标显示中位数与 MAD；
诊断项不作为性能门禁，缺失数据不算通过。

CI 仅上传 benchmark 数据 artifact；图表按需在本地生成，不自动上传图片或发布 PR 图片评论。

本地使用下载的真实 CI evidence 生成预览：

```powershell
gh run download RUN_ID --repo qigao/salts --pattern 'io-benchmark-*' --dir benchmark-inputs
python cmake/ci/render-benchmark-report.py --input benchmark-inputs --output benchmark-report --head HEAD_SHA --run RUN_ID --conclusion success
python -m unittest discover -s cmake/ci/tests
```

将 `RUN_ID`、`HEAD_SHA`、`--conclusion` 替换为实际运行信息；本地 Python 需要 Matplotlib。
`benchmark-metrics.json` 必须来自采用新报告流程的运行，旧 artifact 缺少此文件时标记为 missing。
