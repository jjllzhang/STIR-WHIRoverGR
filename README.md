# STIR & WHIR over Galois Rings

## 说明

STIR: Reed–Solomon Proximity Testing with Fewer Queries：对 FRI 进行改进，提出 shift to improve rate 思想，代码实现：https://github.com/WizardOfMenlo/stir。

WHIR: Reed–Solomon Proximity Testing with Super-Fast Verification：将 STIR 中的单变量多项式承诺方案推广到多变量多项式承诺方案，RS码推广到CRS码，代码实现：https://github.com/WizardOfMenlo/whir。

BaseFold: Efficient Field-Agnostic Polynomial Commitment Schemes from Foldable Codes：提出foldable codes，将FRI需要的RS码推广到 foldable codes，不需要底层域满足smooth条件，代码地址：https://github.com/jjllzhang/BasefoldOverGR。

Polynomial Commitments for Galois Rings and Applications to SNARKs Over $$\mathbb {Z}_{2^k}$$：将有限域上的FRI推广到 Galois ring上，代码实现：https://github.com/jjllzhang/z2kSNARK。

Polylogarithmic Proofs for Multilinears over $\mathbb{Z}_{2^k}$：将Basefold 推广到 Galois ring上，将FRI over Galois ring 改进到 STIR/WHIR 版本，Basefold部分的代码实现：https://github.com/jjllzhang/BasefoldOverGR。

目标：实现 STIR and WHIR over Galois ring。

当前仓库先对齐 `stir-over-gr-implementation-plan.md` 的工程骨架，目标是：

- 使用 **C++ / CMake** 建立主工程；
- 仅 vendor `BasefoldOverGR` 中的 `GaloisRing` 后端；
- 先把 `algebra / domain / poly_utils / crypto / protocols` 的目录与接口搭起来；
- 优先完成 `GRContext`、序列化与基础测试，给后续 `FRI-3 / FRI-9 / STIR(9->3)` 留出清晰落点。

## 当前状态

- `third_party/GaloisRing/` 已纳入 `GaloisRing` 后端源码；
- `include/` 与 `src/` 已按实施计划搭好模块层次；
- `GRContext`、ring element 序列化、`Domain`、`Polynomial`、GR 插值 wrapper 已给出第一版实现；
- `Domain` 现已对 `offset`/`root` 做 fail-fast 校验，并检查 `root` 的 exact order；
- `FFT3` / `inverse_fft3` 与 `folding` 已给出第一版语义正确实现，并补上基础 roundtrip / 对拍测试；
- `rs_encode` / `rs_interpolate` 在 `3-smooth` 域上已默认切到 `fft3` / `inverse_fft3` 快速路径；
- `fft3` / `inverse_fft3` 已补上第一版调用内 stage cache，复用 radix-3 每层的 `offset/root/zeta` 与逆元常量；
- `GRContext` 已提供 `batch_inv`，`interpolate_for_gr_wrapper` 与 generic `fold_eval_k` 已改成 batched inversion 路径，减少 GR 上的分母求逆次数；
- `folding` 已加入 multiplicative-coset fast path，避免在每个 fiber 内重复做 `Inv`；
- `FRI-3` / `FRI-9` / `STIR(9->3)` 已接入 **真实 transcript + Merkle root/opening** 的 non-interactive 路径，并通过 honest / tamper 回归；
- `crypto/hash`、`crypto/fs/transcript`、`crypto/merkle_tree` 已完成第一版实现；当前已 vendoring 官方 `BLAKE3` C 后端，并已将仓库内哈希实现统一收口为 `BLAKE3`；
- Phase 7 第一版已补上可选 OpenMP Merkle 并行化；`SWGR_USE_OPENMP=ON` 且本机找到 OpenMP 时，leaf / parent hashing 会自动并行；
- CMake target 命名已统一为 `galoisring_backend` 与 `stir_over_gr`；
- Phase 1 域构造推荐从 `Domain::teichmuller_subgroup(...)` / `Domain::teichmuller_coset(...)` 进入；
- `bench_proof_size_estimate` 与 `bench_time` 已支持 `FRI-3 / FRI-9 / STIR(9->3)` 对照输出，并补了 `scripts/run_proof_size_estimator_from_preset.sh`、`scripts/run_timing_benchmark_from_preset.sh` 与 `scripts/plot_benchmark_metric_by_protocol.py`；
- 已补第一版工程型 `soundness configurator`：当前统一输出 `soundness_model / query_policy / pow_policy / effective_security_bits / soundness_notes`，明确把自动查询调度与 `lambda/pow_bits` 口径标成 `engineering-heuristic-v1`；
- 已补 `scripts/search_benchmark_parameters.py` 与 `scripts/run_benchmark_parameter_search.sh`，可在 preset 基础上做参数枚举、聚合 size/time 结果，并输出 Top-K / Pareto 摘要。

## 构建

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

如需显式关闭本轮加入的 OpenMP Merkle 并行化，可使用：

```bash
cmake -S . -B build -DSWGR_USE_OPENMP=OFF
```

### Phase 6 快速验证

```bash
ctest --test-dir build --output-on-failure -R 'test_crypto|test_fri|test_stir'
./build/bench_proof_size_estimate --protocol all --format csv
./build/bench_time --protocol all --format csv
./scripts/run_proof_size_estimator_from_preset.sh --output results/size_latest.csv
./scripts/run_timing_benchmark_from_preset.sh --output results/time_latest.csv
./scripts/run_benchmark_parameter_search.sh --n-values 81,243 --rho-values 1/3,1/9 --soundness 128:22:ConjectureCapacity:auto
./scripts/run_proof_size_estimator_from_preset.sh --preset bench/presets/smallest_workload_covering_all_protocols_gr216_r54.json --build-dir build --output results/smallest_workload_covering_all_protocols_gr216_r54_proof_size.csv
./scripts/run_timing_benchmark_from_preset.sh --preset bench/presets/main_benchmark_workload_for_timing_gr216_r162.json --build-dir build-release --threads 1 --output results/main_benchmark_workload_gr216_r162_timing.csv --warmup 1 --reps 3
```

说明：

- 当前 `FRI-3` / `FRI-9` / `STIR(9->3)` prover/verifier 已使用真实 `Transcript` 与 `MerkleTree::open(...)`；
- `bench_proof_size_estimate` 仍按计划输出 **compiled argument size estimator**，不是逐字节真实序列化 proof 大小；
- `bench_proof_size_estimate` 现额外输出 `transcript_challenge_count / transcript_bytes_estimated / pow_nonce_bytes` 三个 estimator 元数据字段；它们用于描述 transcript/PoW 口径，不并入 `estimated_argument_bytes`；
- `bench_proof_size_estimate` 与 `bench_time` 现都会输出工程版 soundness 元数据：`soundness_model / query_policy / pow_policy / effective_security_bits / soundness_notes`；
- Batch 4 的归档证据已落在 `results/smallest_workload_covering_all_protocols_gr216_r54_{proof_size,timing}.csv` 与 `results/main_benchmark_workload_gr216_r162_{proof_size,timing}.csv`；复现命令见 `results/README.md`；
- Batch 5 已补 `results/workload_showing_two_stir_rounds_gr216_r486_{proof_size,timing}.csv`；该组在 `GR(2^16,486), n=729, d=243` 上覆盖两轮 STIR estimator 与 time bench，但当前口径下 `stir9to3` 既未在 size 上优于 `fri9`（`36.570 KiB` vs `29.926 KiB`），在 time 侧也更重（`prover_total_ms = 7370.485` vs `3930.196`，`verify_ms = 8240.489` vs `324.210`），因此仍作为参数/趋势归档，而非第一版主图；
- `--queries` 支持 `auto` 或显式 `q0[,q1,...]`；若某轮请求值被 cap，`bench_proof_size_estimate` / `bench_time` 会在 `stderr` 打 warning，estimator 的 `round_breakdown_json` 会写出 `requested_query_count / effective_query_count / cap_applied`；
- `bench_time --threads` 现会直接控制 OpenMP 运行时线程数；`scripts/run_timing_benchmark_from_preset.sh` 会同步注入 `OMP_NUM_THREADS=<threads>` 与 `OMP_DYNAMIC=false`，不再只是输出元数据；
- `bench_time` 支持 `--warmup` / `--reps`；headline 时间字段按 measured reps 求均值；
- `bench_time` 会输出 `commit_ms / prove_query_phase_ms / prover_total_ms / verify_ms / verifier_hashes_actual`，并额外带上 `serialized_bytes_actual / serialized_kib_actual`；
- 细粒度 profile 同时给出 `profile_*_total_ms`（measured reps 累积）与 `profile_*_mean_ms`（单次 measured rep 均值），并补 `profile_*_accounted_*` / `profile_*_unaccounted_*` 便于对账；
- `search_benchmark_parameters.py` 当前实现的是**工程型**参数搜索器：它复用现有 bench 二进制，给出候选全集、Top-K 与 Pareto 摘要；其中 `soundness_model = engineering-heuristic-v1` 仅表示当前启发式口径，后续可在形式化 soundness 推导补齐后替换；
- `plot_benchmark_metric_by_protocol.py` 依赖 `matplotlib`；若本机未安装，可先执行 `python3 -m pip install matplotlib`。

## 目录

核心目录与 `stir-over-gr-implementation-plan.md` 对齐：

- `third_party/GaloisRing/`: 复用的 Galois ring 后端
- `include/algebra/`: Galois ring 上下文、Teichmüller 相关接口
- `include/poly_utils/`: 多项式、插值、FFT / folding 基础实现与后续扩展接口
- `include/fri/`, `include/stir/`, `include/whir/`: 协议层入口
- `bench/`, `tests/`, `scripts/`, `results/`: bench、测试、脚本与产物目录

## 库名

- `galoisring_backend`: vendor 的 `GaloisRing` 后端静态库
- `stir_over_gr`: 本项目主静态库
- 兼容 alias 仍保留 `swgr::swgr`

## Phase 1 接口

- `algebra::teichmuller_subgroup_generator(...)`: 为给定 `N | (p^r - 1)` 生成对应 Teichmüller 子群生成元
- `Domain::teichmuller_subgroup(...)`: 直接构造大小为 `N` 的 Teichmüller 子群域
- `Domain::teichmuller_coset(...)`: 直接构造大小为 `N` 的 Teichmüller 子群陪集域
