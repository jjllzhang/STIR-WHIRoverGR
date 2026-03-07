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
- `folding` 已加入 multiplicative-coset fast path，避免在每个 fiber 内重复做 `Inv`；
- `FRI-3` / `FRI-9` / `STIR(9->3)` 已接入 **真实 transcript + Merkle root/opening** 的 non-interactive 路径，并通过 honest / tamper 回归；
- `crypto/hash`、`crypto/fs/transcript`、`crypto/merkle_tree` 已完成第一版实现；当前在无 `BLAKE3` 依赖的环境下，`STIR_NATIVE / WHIR_NATIVE` 先以 OpenSSL 后端做 role-separated fallback；
- Phase 7 第一版已补上可选 OpenMP Merkle 并行化；`SWGR_USE_OPENMP=ON` 且本机找到 OpenMP 时，leaf / parent hashing 会自动并行；
- CMake target 命名已统一为 `galoisring_backend` 与 `stir_over_gr`；
- Phase 1 域构造推荐从 `Domain::teichmuller_subgroup(...)` / `Domain::teichmuller_coset(...)` 进入；
- `bench_proof_size_estimate` 与 `bench_time` 已支持 `FRI-3 / FRI-9 / STIR(9->3)` 对照输出，并补了 `scripts/run_bench_size.sh`、`scripts/run_bench_time.sh` 与 `scripts/plot_bench_results.py`。

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
./scripts/run_bench_size.sh --output results/size_latest.csv
./scripts/run_bench_time.sh --output results/time_latest.csv
```

说明：

- 当前 `FRI-3` / `FRI-9` / `STIR(9->3)` prover/verifier 已使用真实 `Transcript` 与 `MerkleTree::open(...)`；
- `bench_proof_size_estimate` 仍按计划输出 **compiled argument size estimator**，不是逐字节真实序列化 proof 大小；
- `bench_time` 支持 `--warmup` / `--reps`；headline 时间字段按 measured reps 求均值；
- `bench_time` 会输出 `commit_ms / prove_query_phase_ms / prover_total_ms / verify_ms / verifier_hashes_actual`；
- 细粒度 profile 同时给出 `profile_*_total_ms`（measured reps 累积）与 `profile_*_mean_ms`（单次 measured rep 均值），并补 `profile_*_accounted_*` / `profile_*_unaccounted_*` 便于对账；
- `plot_bench_results.py` 依赖 `matplotlib`；若本机未安装，可先执行 `python3 -m pip install matplotlib`。

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
