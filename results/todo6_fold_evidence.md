# TODO 6：`fold_table_k(...)` 并行化证据

## 目标
- 为 Batch 6 的 `fold_table_k(...)` worker-local `NTL context restore` 并行化补齐可归档证据。
- 先确认 kernel 级 micro-bench 是否确实有收益，再看一组真实 `bench_time` workload 上的 bucket 变化。
- 保持结论边界：本批次可以宣称 `fold_table_k` kernel 已获得稳定正收益，但还不提前宣称“多线程优化总项完成”。

## 已归档文件
- `results/todo6_fold_n243_t1.csv`
- `results/todo6_fold_n243_t8.csv`
- `results/todo6_fold_n729_t1.csv`
- `results/todo6_fold_n729_t8.csv`
- `results/todo6_fold_summary.csv`
- `results/todo6_time_t1.csv`
- `results/todo6_time_t8.csv`

## 可复现命令

### 1. 构建
```bash
cmake --build build-release --target test_folding bench_fold_table bench_time -j4
ctest --test-dir build-release --output-on-failure -R 'test_folding|test_gr_basic|test_fri|test_stir'
```

### 2. `fold_table_k(...)` micro-bench
```bash
./results/run_todo6_fold_sweep.sh
```

### 3. `bench_time` 线程对照
```bash
./scripts/run_bench_time.sh --build-dir build-release --protocol fri9,stir9to3 --threads 1 --warmup 1 --reps 3 --format csv --output results/todo6_time_t1.csv
./scripts/run_bench_time.sh --build-dir build-release --protocol fri9,stir9to3 --threads 8 --warmup 1 --reps 3 --format csv --output results/todo6_time_t8.csv
```

说明：
- `bench_time` 默认走 `bench/presets/jia_micro_gr216_r162.json`，即 `GR(2^16,162), n=243, d=81`。
- wrapper 会同步设置 `OMP_NUM_THREADS=<threads>` 与 `OMP_DYNAMIC=false`。

## Micro-bench 结果：`fold_table_k(...)`

| case | ring | n | folded_n | threads | mean_ms | speedup_vs_t1 | checksum |
|---|---|---:|---:|---:|---:|---:|---:|
| n243_t1 | `GR(2^16,162)` | 243 | 27 | 1 | 86.060 | 1.000x | 6350165810735399646 |
| n243_t8 | `GR(2^16,162)` | 243 | 27 | 8 | 32.081 | 2.683x | 6350165810735399646 |
| n729_t1 | `GR(2^16,486)` | 729 | 81 | 1 | 721.900 | 1.000x | 9357501018531900761 |
| n729_t8 | `GR(2^16,486)` | 729 | 81 | 8 | 171.665 | 4.205x | 9357501018531900761 |

结论：
- `fold_table_k(...)` 这条 kernel 在两组规模上都出现稳定正收益。
- `n=729` 的收益更明显，说明该并行版本在较大 folded table 上更接近“计算主导”而不是“调度主导”。
- `checksum` 完全一致，说明 1/8 线程下输出语义没有漂移。

## `bench_time` 结果：`threads=1` vs `threads=8`

口径：
- 协议只看 `fri9` 与 `stir9to3`。
- workload 固定为 `GR(2^16,162), n=243, d=81`。
- headline 看 `prover_total_ms` / `verify_ms`，同时抽取 `profile_prover_fold_mean_ms` 观察 fold bucket 是否下降。

| protocol | threads | commit_ms | prover_fold_mean_ms | prover_encode_mean_ms | prover_total_ms | verify_ms | verify_algebra_mean_ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| fri9 | 1 | 2.272 | 85.574 | 273.956 | 386.775 | 52.972 | 23.727 |
| fri9 | 8 | 2.282 | 32.778 | 274.346 | 334.890 | 53.563 | 23.819 |
| stir9to3 | 1 | 202.462 | 85.889 | 362.377 | 558.978 | 641.079 | 612.887 |
| stir9to3 | 8 | 169.533 | 52.181 | 363.731 | 527.496 | 611.937 | 582.778 |

### speedup 摘要

| protocol | prover_fold_speedup | prover_total_speedup | verify_speedup | 备注 |
|---|---:|---:|---:|---|
| fri9 | 2.611x | 1.155x | 0.989x | fold bucket 明显下降，但 encode / verify 基本不动 |
| stir9to3 | 1.646x | 1.060x | 1.048x | fold 有收益，另有 commit / verify 小幅改善 |

## 结论
- 这批结果已经足够支持 **“`fold_table_k(...)` 并行 kernel 已落地并有稳定收益”**。
- `bench_time` 上也能看到真实 workload 的正向变化，但目前仍是 **局部 bucket gain > 端到端 total gain**：
  - `fri9` 的 `profile_prover_fold_mean_ms` 从 `85.574` 降到 `32.778`，但 `prover_total_ms` 只提升到 `1.155x`，因为 encode 仍是更大的常驻桶。
  - `stir9to3` 的 `profile_prover_fold_mean_ms` 从 `85.889` 降到 `52.181`，`prover_total_ms` 只到 `1.060x`；说明真实 pipeline 仍受其他 bucket 限制。
- 因此当前最准确的 Batch 6 结论是：
  - **kernel win**
  - **end-to-end positive but still pipeline-limited**
  - **“多线程优化”总项仍不提前勾选**

## 下一步建议
- 先把相同的 worker-local `NTL context restore` 模式迁移到 `fft3(...) / inverse_fft3(...)` 的顶层可并行块，而不是继续在 `fold_table_k(...)` 上做更激进的小修补。
- 若要继续扩大端到端收益，优先看 `profile_prover_encode_mean_ms` 对应的 `fft3 / inverse_fft3 / rs_encode` 路径，因为它在 `fri9` 上仍然明显大于 fold bucket。
