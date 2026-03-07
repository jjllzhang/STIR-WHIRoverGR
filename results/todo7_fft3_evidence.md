# TODO 7：`fft3(...) / inverse_fft3(...)` stage-loop 并行化证据

## 目标
- 为 Batch 7 的 `fft3(...) / inverse_fft3(...)` stage-local 并行化补齐可归档证据。
- 先确认 `bench_fft3` 的 kernel 级 `encode / interpolate` 是否确实获得收益，再看 `bench_time --threads 1/8` 中 `encode / interpolate` bucket 是否同步下降。
- 保持结论边界：本批次可以宣称 **`fft3` stage-loop 多线程版已落地**，但不能把“micro-bench 有收益”和“所有端到端 bucket 都同步改善”混为一谈。

## 已归档文件
- `results/todo7_fft3_encode_n243_t1.csv`
- `results/todo7_fft3_encode_n243_t8.csv`
- `results/todo7_fft3_interpolate_n243_t1.csv`
- `results/todo7_fft3_interpolate_n243_t8.csv`
- `results/todo7_fft3_encode_n729_t1.csv`
- `results/todo7_fft3_encode_n729_t8.csv`
- `results/todo7_fft3_interpolate_n729_t1.csv`
- `results/todo7_fft3_interpolate_n729_t8.csv`
- `results/todo7_fft3_summary.csv`
- `results/todo7_time_t1.csv`
- `results/todo7_time_t8.csv`

## 可复现命令

### 1. 构建
```bash
cmake -S . -B build-release
cmake --build build-release --target test_fft3 test_fri test_stir bench_time bench_fft3 -j4
```

### 2. `fft3(...) / inverse_fft3(...)` micro-bench
```bash
./results/run_todo7_fft3_sweep.sh
```

### 3. `bench_time` 线程对照
```bash
./scripts/run_bench_time.sh --build-dir build-release --protocol fri9,stir9to3 --threads 1 --warmup 1 --reps 3 --format csv --output results/todo7_time_t1.csv
./scripts/run_bench_time.sh --build-dir build-release --protocol fri9,stir9to3 --threads 8 --warmup 1 --reps 3 --format csv --output results/todo7_time_t8.csv
```

说明：
- `bench_fft3` 当前默认使用 `d=n/3`，与仓库常用 `rho=1/3` workload 对齐；因此 `n=243` 时默认 `d=81`，`n=729` 时默认 `d=243`。
- `bench_time` 默认走 `bench/presets/jia_micro_gr216_r162.json`，即 `GR(2^16,162), n=243, d=81`。
- wrapper 会同步设置 `OMP_NUM_THREADS=<threads>` 与 `OMP_DYNAMIC=false`。

## Micro-bench 结果：`bench_fft3`

| case | mode | ring | n | d | input_size | threads | mean_ms | speedup_vs_t1 | checksum |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| encode_n243_t1 | `encode` | `GR(2^16,162)` | 243 | 81 | 81 | 1 | 273.696 | 1.000x | 17634618731071157015 |
| encode_n243_t8 | `encode` | `GR(2^16,162)` | 243 | 81 | 81 | 8 | 209.574 | 1.306x | 17634618731071157015 |
| interpolate_n243_t1 | `interpolate` | `GR(2^16,162)` | 243 | 81 | 243 | 1 | 229.912 | 1.000x | 13535206615212669415 |
| interpolate_n243_t8 | `interpolate` | `GR(2^16,162)` | 243 | 81 | 243 | 8 | 186.971 | 1.230x | 13535206615212669415 |
| encode_n729_t1 | `encode` | `GR(2^16,486)` | 729 | 243 | 243 | 1 | 2910.279 | 1.000x | 296489418278617069 |
| encode_n729_t8 | `encode` | `GR(2^16,486)` | 729 | 243 | 243 | 8 | 1560.981 | 1.864x | 296489418278617069 |
| interpolate_n729_t1 | `interpolate` | `GR(2^16,486)` | 729 | 243 | 729 | 1 | 2354.270 | 1.000x | 6394904769582110993 |
| interpolate_n729_t8 | `interpolate` | `GR(2^16,486)` | 729 | 243 | 729 | 8 | 1487.052 | 1.583x | 6394904769582110993 |

结论：
- `bench_fft3` 已明确给出 kernel 级正收益，且规模越大收益越明显。
- `encode` 的收益普遍强于 `interpolate`，但二者都不是“只有噪声”的量级。
- 补上 serial-loop bypass 之后，`threads=1` 与 `threads=8` 都比上一版 Batch 7 数字更好：`n=243` 的 encode/interpolate 分别再降约 `14.7 ms / 21.1 ms`，`n=729` 的 encode/interpolate 分别再降约 `121.4 ms / 172.2 ms`。
- `checksum` 全部一致，说明 1/8 线程下语义没有漂移。

## `bench_time` 结果：`threads=1` vs `threads=8`

口径：
- 协议只看 `fri9` 与 `stir9to3`。
- workload 固定为 `GR(2^16,162), n=243, d=81`。
- headline 重点看 `profile_prover_encode_mean_ms` / `profile_prover_interpolate_mean_ms`，同时保留 `prover_total_ms` / `verify_ms` / `profile_verify_algebra_mean_ms`。

| protocol | threads | prover_encode_mean_ms | prover_interpolate_mean_ms | prover_fold_mean_ms | prover_total_ms | verify_ms | verify_algebra_mean_ms |
|---|---:|---:|---:|---:|---:|---:|---:|
| fri9 | 1 | 270.285 | 22.117 | 84.909 | 382.208 | 52.285 | 23.391 |
| fri9 | 8 | 190.069 | 22.159 | 38.454 | 256.138 | 52.385 | 23.444 |
| stir9to3 | 1 | 356.495 | 22.094 | 84.497 | 550.144 | 631.619 | 603.804 |
| stir9to3 | 8 | 273.723 | 22.259 | 38.059 | 421.767 | 502.639 | 474.560 |

### 当前 1 线程 vs 8 线程 speedup

| protocol | encode_speedup | interpolate_speedup | prover_total_speedup | verify_speedup | verify_algebra_speedup | 备注 |
|---|---:|---:|---:|---:|---:|---|
| fri9 | 1.422x | 0.998x | 1.492x | 0.998x | 0.998x | encode 明显下降；interpolate 基本持平 |
| stir9to3 | 1.302x | 0.993x | 1.304x | 1.257x | 1.272x | encode 明显下降；verifier algebra 也同步受益 |

### 与上一版 Batch 7（未加 serial fast path）相比

| protocol | `threads=1` 变化 | `threads=8` 变化 |
|---|---|---|
| fri9 | `encode 291.337 -> 270.285`，`interpolate 24.781 -> 22.117`，`prover_total 407.414 -> 382.208` | `encode 203.678 -> 190.069`，`interpolate 24.733 -> 22.159`，`prover_total 273.151 -> 256.138` |
| stir9to3 | `encode 385.014 -> 356.495`，`interpolate 24.751 -> 22.094`，`prover_total 585.061 -> 550.144`，`verify 668.797 -> 631.619` | `encode 292.861 -> 273.723`，`interpolate 24.861 -> 22.259`，`prover_total 445.821 -> 421.767`，`verify 529.240 -> 502.639` |

### 与 Batch 6 基线相比

| protocol | `threads=1` (`todo6_t1 -> todo7_t1`) | `threads=8` (`todo6_t8 -> todo7_t8`) |
|---|---|---|
| fri9 | `encode 273.956 -> 270.285`，`interpolate 22.328 -> 22.117`，`prover_total 386.775 -> 382.208` | `encode 274.346 -> 190.069`，`interpolate 22.778 -> 22.159`，`prover_total 334.890 -> 256.138` |
| stir9to3 | `encode 362.377 -> 356.495`，`interpolate 22.450 -> 22.094`，`prover_total 558.978 -> 550.144`，`verify 641.079 -> 631.619` | `encode 363.731 -> 273.723`，`interpolate 22.576 -> 22.259`，`prover_total 527.496 -> 421.767`，`verify 611.937 -> 502.639` |

## 结论
- 这批结果已经足够支持 **“`fft3(...) / inverse_fft3(...)` 的 stage-loop 多线程 kernel 已落地并有稳定收益”**。
- 端到端上，**真正清晰下降的是 `encode` bucket**：
  - `fri9`: `270.285 -> 190.069`（`1.422x`）
  - `stir9to3`: `356.495 -> 273.723`（`1.302x`）
- `interpolate` bucket 在当前 `bench_time` workload 上仍不是 headline，但补上 serial fast path 后，1/8 线程下都已从约 `24.7 ms` 收回到约 `22.1~22.3 ms`，与 Batch 6 基线基本持平或略优。
- `stir9to3` 的 verifier 侧继续出现同步正收益：`verify_ms` 从 `631.619` 降到 `502.639`，`verify_algebra_mean_ms` 从 `603.804` 降到 `474.560`，说明 FFT 路径的优化会传导到 verifier algebra。
- 之前 Batch 7 的“单线程略回升” caveat 现在可以收回：加上 serial-loop bypass 后，当前 `threads=1` 已相对 Batch 6 基线略有改善，而不是继续回退。

## 当前最准确的 Batch 7 结论
- **kernel win**
- **end-to-end positive, primarily via encode**
- **interpolate 已回到 Batch 6 基线附近，但在当前 workload 上仍不是 headline bucket**
- **`threads=1` fast path 已把单线程回归收回**
- **总项“多线程优化”仍不提前勾选**

## 下一步建议
- 当前 `threads=1` fast path 已经补上；若继续追 `fft3` 路径，下一步更值得看的不再是 serial bypass，而是更激进但风险更高的 `fft3` 递归 task-graph / in-place 改写 / cross-call twiddle-root 缓存。
- 若要继续扩大端到端收益，仍应优先盯 `profile_prover_encode_mean_ms` 和 `profile_verify_algebra_mean_ms`，因为这两个 bucket 在当前 Batch 7 后依旧是更显著的剩余大头。
