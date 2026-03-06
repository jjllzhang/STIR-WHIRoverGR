# STIR & WHIR over Galois Rings

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
- `FRI-3` baseline 已按 Phase 3 落地为 **in-memory oracle + deterministic mock transcript** 版本，包含 prover / verifier / proof size estimator；
- CMake target 命名已统一为 `galoisring_backend` 与 `stir_over_gr`；
- Phase 1 域构造推荐从 `Domain::teichmuller_subgroup(...)` / `Domain::teichmuller_coset(...)` 进入；
- `FRI-9`、`STIR(9->3)`、真实 `Merkle + Fiat–Shamir` hash/opening 仍待后续 Phase 继续实现。

## 构建

```bash
cmake -S . -B build
cmake --build build -j
ctest --test-dir build --output-on-failure
```

### Phase 3 快速验证

```bash
ctest --test-dir build --output-on-failure -R test_fri
./build/bench_proof_size_estimate
```

说明：

- 当前 `FRI-3` verifier 使用 mock transcript 重新派生 round challenge 与 query positions；
- proof 仍走 in-memory oracle，不依赖真实 `MerkleTree::open(...)`；
- `bench_proof_size_estimate` 输出的是 Phase 3 口径下的估算值，不是最终真实序列化 proof 大小。

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
