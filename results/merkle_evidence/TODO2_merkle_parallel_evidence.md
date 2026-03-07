# TODO 2: Merkle 并行化证据（线程 sweep）

## 目标
- 仅基于现有 `bench_time` 产出 Merkle 并行化证据。
- 不修改 `bench_time.cpp` 或 `src/crypto/merkle_tree/*`。
- 对比 `OMP_NUM_THREADS=1/2/4/8` 的曲线。

## 可复现命令
```bash
cd /tmp/swgr-mt-20260308-todo2-merkle-evidence
cmake -S . -B build
cmake --build build -j --target bench_time
./results/merkle_evidence/run_merkle_thread_sweep.sh
```

脚本会输出：
- 原始分线程结果：`results/merkle_evidence/raw/{small,large}_t{1,2,4,8}.csv`
- 汇总结果：`results/merkle_evidence/merkle_thread_scaling_summary.csv`

## 口径
- 协议：`stir9to3`
- 小输入（Preset 0 风格）：`GR(2^16,54), n=81, d=27`
- 大输入（Preset 2 风格）：`GR(2^16,486), n=729, d=243`
- bench 参数：`--warmup 1 --reps 1 --format csv`
- 线程控制：每次运行设置 `OMP_NUM_THREADS=t`，并同步传 `--threads t` 用于记录。
- 观察指标：
  - `profile_prover_merkle_mean_ms`
  - `profile_verify_merkle_mean_ms`
  - 辅助参考：`prover_total_ms`, `verify_ms`

## 线程缩放表（Merkle bucket）
| workload | threads | prover_merkle_mean_ms | verify_merkle_mean_ms | prover_merkle_speedup_vs_t1 | verify_merkle_speedup_vs_t1 |
|---|---:|---:|---:|---:|---:|
| large | 1 | 301.823 | 299.598 | 1.000 | 1.000 |
| large | 2 | 299.431 | 298.887 | 1.008 | 1.002 |
| large | 4 | 299.415 | 298.256 | 1.008 | 1.004 |
| large | 8 | 299.077 | 298.158 | 1.009 | 1.005 |
| small | 1 | 3.007 | 3.070 | 1.000 | 1.000 |
| small | 2 | 3.007 | 3.072 | 1.000 | 0.999 |
| small | 4 | 2.996 | 3.058 | 1.004 | 1.004 |
| small | 8 | 3.003 | 3.054 | 1.001 | 1.005 |

## before/after 简短结论表
这里将 `threads=1` 视为 before，`threads=8` 视为 after。

| workload | before (t=1) prover/verify merkle ms | after (t=8) prover/verify merkle ms | 结论 |
|---|---|---|---|
| small | 3.007 / 3.070 | 3.003 / 3.054 | 基本无收益（波动量级） |
| large | 301.823 / 299.598 | 299.077 / 298.158 | 有收益但幅度较小（约 1%） |

## 结论
- 大输入下可观察到正向收益（Merkle bucket 约 `1.005x ~ 1.009x`），但增益较小。
- 小输入下收益有限或无收益，基本处于噪声范围。
- 在本机该组配置中，Merkle bucket 并未成为总时延主导项，因此 `prover_total_ms / verify_ms` 的线程收益更弱。

## 备注（后续可增强点）
- 若要得到更强“线程缩放”证据，可与“线程控制强化”相关分支联动，补充：
  - 更高重复次数（降低测量噪声）
  - 更大工作负载矩阵（更高叶子数/更多轮次）
  - 线程绑定与 NUMA 控制
