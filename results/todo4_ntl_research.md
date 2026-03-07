# TODO 4：NTL / GRContext 并发前置研究

> 更新（2026-03-08）：`GRContext` 的 backend / Teichmüller lazy initialization 现已改成 `std::call_once` 风格，不再保留“共享未预热 `GRContext` 首次进入热点路径存在初始化竞态”这一旧问题。其后又补上了 **worker-local NTL context restore**：`build_oracle_leaves(...)` 现在通过 `GRContext::parallel_for_with_ntl_context(...)` 在每个 worker 内显式恢复上下文，并配合 in-context serializer helper 落地并行 bundle 构建。下面剩余的限制，现主要收敛为：`fold_table_k(...)` / `fft3(...)` 这类更深的 NTL 算术路径仍不能只靠父线程外层包一层 `with_ntl_context(...)` 就直接并行。

## 结论摘要

这轮不建议直接对 `fold_table_k(...)`、`fft3(...) / inverse_fft3(...)` 上并行；`build_oracle_leaves(...)` 这条线已先按 worker-local context restore 的方式落地。

原因不是“算法上不能并行”，而是**当前代码虽然已经补上 `GRContext` 的线程安全 lazy initialization，但 NTL 上下文安装仍是按线程局部（thread-local）语义工作的**：

- 本机 NTL 确实以线程安全模式编译，`ZZ_p` 当前模数和临时空间是 **thread-local** 的，见 `/usr/include/NTL/ZZ_p.h:95`、`/usr/include/NTL/config.h:71`。
- 仓库自己的 `GRContext` 现在已改成 `std::call_once` 风格的 lazy initialization；因此“共享同一个未预热 `GRContext` 首次调用时会竞态初始化”这一点已不再成立。
- `fold_table_k(...)` 与 `fft3(...)` 当前都只在**调用线程**外层包了一次 `with_ntl_context(...)`，随后内部 helper 直接做 NTL 运算；如果把内部循环/递归直接丢到 worker thread，上下文不会自动继承，因为 NTL 当前模数是 thread-local，见 `include/algebra/gr_context.hpp:49`、`src/poly_utils/folding.cpp:300`、`src/poly_utils/fft3.cpp:224`、`src/poly_utils/fft3.cpp:241`。
- `build_oracle_leaves(...)` 已不再只是“理论上更接近可并行”：当前实现已把 bundle 循环改成 worker-local 上下文恢复，并用 in-context serializer 避免每个元素重复进出 `with_ntl_context(...)`；轻量 probe 也证明确实带来了可观收益。

## 现在能做 / 现在别做 / 前置条件

| 热点 | 现在能做 | 现在别做 | 需要什么前置条件 |
| --- | --- | --- | --- |
| `build_oracle_leaves(...)` | 已落地 worker-local context restore + bundle 级并行；可继续沿这条线做 leaf-size / threshold / chunking 调参 | 不要把这套写法原样套到 `fold_table_k(...)` / `fft3(...)`，它们的 NTL 算术依赖更深 | 若继续优化，重点应转向 threshold、chunk 粒度和是否补正式 micro-bench |
| `fold_table_k(...)` | 继续做串行 cache / 算术优化；跨 proof 的粗粒度并行比内层并行更稳妥 | 不要在 `src/poly_utils/folding.cpp:314` 的 base-loop 上直接并行；外层 `with_ntl_context(...)` 只覆盖主线程 | 给 worker 显式恢复 NTL 上下文，再做粒度/收益 bench |
| `fft3(...)` / `inverse_fft3(...)` | 继续做串行 plan/cache/分配优化；保持 3-smooth 快路径 | 不要把 `ForwardRadix3(...)` / `InverseRadix3(...)` 的递归分支直接并行化 | 需要 worker-local NTL context restore；需要确认 plan 中 `GRElem` 跨线程使用的上下文契约；最好再评估任务粒度是否覆盖线程调度开销 |
| `GRContext` 共享使用 | 共享未预热 `GRContext` 的首次 backend / teich 初始化现在可以安全并发触发 | 不要把“初始化竞态已修复”误解成“任意 NTL 路径都可直接并行” | 仍需 worker-local NTL context restore，才能把更多算术路径升级为正式并行 TODO |

## 代码证据

### 1. `GRContext` 的初始化模型已修成线程安全 lazy initialization

- `with_ntl_context(...)` 仍是先 `ensure_backend_initialized()`，然后在**当前线程**里做 `ZZ_pPush` / `ZZ_pEPush`；这一点没有变。
- 现在 `GRContext` 已把 backend / teich 的 lazy initialization 放进 `std::call_once`，因此首次并发使用共享 `GRContext` 时，不再有原先那种“普通 bool + 无锁写入”的初始化竞态。
- 这次修复解决的是“共享上下文首次 lazy init”的线程安全问题，不等于自动解决“父线程装好的 NTL context 能否被子线程继承”。

### 2. NTL 当前上下文是 thread-local，不会因为父线程包了一层 `with_ntl_context(...)` 就自动给子线程生效

- 安装在本机的 NTL 打开了 `NTL_THREADS` 与 `NTL_THREAD_BOOST`，见 `/usr/include/NTL/config.h:71`、`/usr/include/NTL/config.h:90`。
- `ZZ_p` 当前模数信息和临时空间是 thread-local，见 `/usr/include/NTL/ZZ_p.h:95`。
- `ZZ_pPush` / `ZZ_pEPush` 是“保存当前线程上下文，再恢复新上下文”的 RAII 壳，见 `/usr/include/NTL/ZZ_p.h:151`、`/usr/include/NTL/ZZ_pE.h:83`。

这意味着：

- 在父线程外层做一次 `with_ntl_context(...)`，只保证父线程内的 NTL 运算有正确上下文。
- 如果把内部循环/递归丢给 worker thread，worker 必须自己恢复上下文；否则它看到的是它自己的 thread-local 当前模数，而不是父线程的。

### 3. `fold_table_k(...)` 现在的结构不适合“就地并行”

- 顶层直接包住整段计算：`domain.context().with_ntl_context([&] { ... })`，见 `src/poly_utils/folding.cpp:300`。
- 真正可并行的 base-loop 在这个闭包内部，见 `src/poly_utils/folding.cpp:314`。
- 闭包内部大量用原生 NTL/GR 运算：`power(...)`、`Inv(...)`、`EvaluateStructuredFiber(...)`，见 `src/poly_utils/folding.cpp:302`、`src/poly_utils/folding.cpp:320`。

结论：如果在 `base` 循环直接加并行 pragma，worker thread 不会自动拥有当前闭包安装的 NTL 上下文；要并行就得重构成“每个 worker 自己进入上下文”。

### 4. `fft3(...) / inverse_fft3(...)` 也是同类问题

- 顶层各自只包一层 `with_ntl_context(...)`，见 `src/poly_utils/fft3.cpp:224`、`src/poly_utils/fft3.cpp:241`。
- 递归 helper `ForwardRadix3(...)` / `InverseRadix3(...)` 内部直接做 `GRElem` 乘加、`power(...)` 派生出的 plan 访问，见 `src/poly_utils/fft3.cpp:128`、`src/poly_utils/fft3.cpp:167`。
- `BuildRadix3Plan(...)` 还缓存了 `offset/root/zeta/inv` 等 `GRElem`，见 `src/poly_utils/fft3.cpp:81`。

结论：算法树形结构确实天然可并行，但当前实现把上下文假设埋在单线程递归里；如果要并行分支，需要给每个 worker 明确装载上下文，并确认 plan/cache 的跨线程读使用契约。

### 5. `build_oracle_leaves(...)` 的瓶颈起点是“NTL 序列化”，因此需要 worker-local 序列化方案，而不是只盯 Merkle 哈希

- 旧版本叶子构建串行遍历 bundle，并且每个元素都重新走一遍 `ctx.serialize(...)`。
- 现在 `build_oracle_leaves(...)` 已改为通过 `GRContext::parallel_for_with_ntl_context(...)` 在每个 worker 内恢复一次上下文，再复用 in-context serializer helper 直接写 bundle payload，避免元素级反复 push/pop。
- 这说明真正有效的前置不是“只给 Merkle 加 OpenMP”，而是先把 NTL thread-local context 的恢复粒度降到 worker 级。
- 相比之下，`MerkleTree` 的叶哈希与父哈希已经是纯字节路径，并已有 OpenMP 边界，见 `src/crypto/merkle_tree/merkle_tree.cpp:70`、`src/crypto/merkle_tree/merkle_tree.cpp:84`。

## 轻量实验

### A. 现有大桶定位

命令：

```bash
OMP_NUM_THREADS=1 ./build-release/bench_time \
  --protocol stir9to3 --p 2 --k-exp 16 --r 54 \
  --n 81 --d 80 --warmup 0 --reps 1 --format text
```

备注：

- 这里用 `n=81`，不是 `729`，因为对 `GR(2^16,54)` 来说 `729` 不整除 `2^54 - 1`，域本身不合法。

结果摘要：

- `profile_prover_encode_total_ms=29.703`
- `profile_prover_fold_total_ms=9.235`
- `profile_prover_interpolate_total_ms=2.591`
- `profile_prover_merkle_total_ms=2.444`

含义：

- 这组 release bucket 说明继续把研究重点放在 `encode/fold/interpolate` 是对的。
- 也说明 Merkle 不是当前最大热点；对 `build_oracle_leaves(...)` 的研究重点应放在“序列化是否可安全并行”，不是重复优化 hash。

### B. 叶子构建 vs Merkle 哈希拆分 probe

新增文件：

- 源码：`results/todo4_leaf_probe.cpp`
- 输出：`results/todo4_leaf_probe_omp1.txt`
- 输出：`results/todo4_leaf_probe_omp8.txt`

编译命令：

```bash
c++ -std=c++20 -O3 -fopenmp \
  -I'/tmp/swgr-mt-20260308-todo4-ntl-research/include' \
  -I'/tmp/swgr-mt-20260308-todo4-ntl-research/src' \
  -I'/tmp/swgr-mt-20260308-todo4-ntl-research/third_party/GaloisRing/include' \
  -I'/tmp/swgr-mt-20260308-todo4-ntl-research/third_party/blake3' \
  '/tmp/swgr-mt-20260308-todo4-ntl-research/results/todo4_leaf_probe.cpp' \
  '/home/zjl/STIR&WHIRoverGR/build-release/libstir_over_gr.a' \
  '/home/zjl/STIR&WHIRoverGR/build-release/libgaloisring_backend.a' \
  '/home/zjl/STIR&WHIRoverGR/build-release/libswgr_blake3.a' \
  -lntl -lgmp -lm -pthread \
  -o '/tmp/swgr-mt-20260308-todo4-ntl-research/results/todo4_leaf_probe'
```

运行命令：

```bash
OMP_NUM_THREADS=1 results/todo4_leaf_probe
OMP_NUM_THREADS=8 results/todo4_leaf_probe
```

probe 配置：

- ring：`GR(2^16,162)`
- domain size：`243`
- element bytes：`324`
- 测试内容：只拆 `build_oracle_leaves(...)` 与 `MerkleTree(leaves)`，避免和 RS encode / fold 混在一起。

结果（基线，worker-local restore 落地前）：

#### `OMP_NUM_THREADS=1`

- `bundle_size=1`
  - `build_oracle_leaves_mean_ms=22.088`
  - `merkle_from_prebuilt_leaves_mean_ms=0.223`
  - 比值约 `98.972x`
- `bundle_size=9`
  - `build_oracle_leaves_mean_ms=22.125`
  - `merkle_from_prebuilt_leaves_mean_ms=0.089`
  - 比值约 `248.424x`

#### `OMP_NUM_THREADS=8`

- `bundle_size=1`
  - `build_oracle_leaves_mean_ms=22.095`
  - `merkle_from_prebuilt_leaves_mean_ms=0.295`
  - 比值约 `74.854x`
- `bundle_size=9`
  - `build_oracle_leaves_mean_ms=22.064`
  - `merkle_from_prebuilt_leaves_mean_ms=0.092`
  - 比值约 `240.024x`

### 实验解释（基线）

- 叶子构建时间几乎等于裸 `serialize` 循环时间，说明 `build_oracle_leaves(...)` 目前几乎就是 **NTL 序列化成本**。
- 同一 probe 下，即使把 `OMP_NUM_THREADS` 从 1 调到 8，叶子构建时间也几乎不变，因为它根本没并行；而 Merkle 哈希本来就很小。
- 因此，当时的正确结论不是“叶子构建永远不值得并行”，而是“先把上下文安全问题解决掉，再谈收益”。

## 后续更新：worker-local context restore 已落地

代码变化：

- `include/algebra/gr_context.hpp`：新增 `GRContext::parallel_for_with_ntl_context(...)`，让每个 worker 自己安装 NTL 上下文。
- `src/fri/common.cpp`：`build_oracle_leaves(...)` 现在走 worker-local 并行 bundle 循环，`bundle_payload(...)` 改用 in-context serializer helper，`serialize_oracle_bundle(...)` 仍保留单独的上下文保护。
- `tests/test_fri.cpp`：新增 `TestBuildOracleLeavesMatchesBundleSerialization`，在 `bundle_count=128` 的规模上校验并行叶子构建结果与逐 bundle 序列化完全一致。

同一 probe 的新结果：

### `OMP_NUM_THREADS=1`

- `bundle_size=1`
  - `build_oracle_leaves_mean_ms=2.342`
- `bundle_size=9`
  - `build_oracle_leaves_mean_ms=2.358`

### `OMP_NUM_THREADS=8`

- `bundle_size=1`
  - `build_oracle_leaves_mean_ms=0.821`
- `bundle_size=9`
  - `build_oracle_leaves_mean_ms=2.363`

结论：

- 即使只看 `OMP_NUM_THREADS=1`，由于去掉了“每个元素都重新进出 `with_ntl_context(...)`”的开销，`build_oracle_leaves(...)` 也从约 `22 ms` 降到约 `2.34 ms`。
- 当 `leaf_count` 足够大并触发并行阈值时（这里 `bundle_size=1, leaf_count=243`），`OMP_NUM_THREADS=8` 还能进一步降到约 `0.82 ms`。
- `bundle_size=9, leaf_count=27` 没跨过当前并行阈值，因此几乎只体现 serializer 粒度优化，不体现线程级加速；这也说明后续若还要继续打磨，应该围绕阈值/粒度而不是“是否能并行”本身。

## 为什么这轮建议“先研究，不要直接上并行 FFT/folding”

1. **上下文继承问题是真约束，不是实现细节。**  
   现在 `fft3(...)` / `fold_table_k(...)` 都是“主线程进上下文，内部 helper 默认上下文已就绪”的写法。对这种结构直接加多线程，很容易做成“看起来能编译、语义却没被上下文保护”的版本。

2. **共享 `GRContext` 的首次 lazy init 竞态已修，但这不是全部前置条件。**  
   即使初始化本身已改成 `std::call_once`，worker thread 仍不会自动继承父线程装好的 NTL thread-local 上下文。

3. **仓库目前没有接入 NTL 原生线程池。**  
   repo 搜索没有看到 `SetNumThreads` / `BasicThreadPool` / `NTL_EXEC_RANGE` 使用，因此“让 NTL 自己并行起来”在当前代码里不是现成按钮。

4. **热点优先级也支持先研究。**  
   当前 `encode/fold/interpolate` 虽然是大桶，但它们和 NTL 上下文假设绑得很深；相比之下，已落地的安全并行边界仍然是纯字节哈希路径。

## 后续建议

### 建议先做

1. 把 `build_oracle_leaves(...)` 这套 **显式 worker-local 上下文恢复** 模式，整理成后续 `fold_table_k(...)` / `fft3(...)` 可复用的并行重构模板。
2. 单独补一个正式 micro-bench，拆出：
   - `serialize span<GRElem>`
   - `build_oracle_leaves(...)`
   - `fft3(...)`
   - `inverse_fft3(...)`
   - `fold_table_k(...)`
3. 如果后续还追 `build_oracle_leaves(...)`，优先补 threshold / chunk 粒度 bench，而不是再论证一次“能不能并行”。

### 在这些前置条件达成前，不建议做

1. 不建议在 `fold_table_k(...)` 的 `base` 循环上直接加 OpenMP。
2. 不建议在 `fft3(...)` / `inverse_fft3(...)` 的递归分支上直接并行。
3. 不建议把“初始化竞态已修复”直接等同于“共享 `GRContext` 的所有算术热点都已可安全并行”。
