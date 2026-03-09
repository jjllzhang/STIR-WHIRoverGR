# FRI PCS 论文对齐执行计划

更新时间：2026-03-10
对齐目标：`mineru-md/Z2KSNARK/hybrid_auto/Z2KSNARK.md:520`

## 1. 计划目标

本计划的目标不是做一次“表面瘦身”，而是把当前仓库里的 `FRI over GR` 从：

- “带真实 `Transcript + Merkle` 的原型 low-degree test / heavy-witness verifier”

逐步推进到：

- “更贴近论文 4.1 的 `FRI-based PCS` 开口协议”

并且让后续进度可以按阶段追踪、按验收标准收口。

## 2. 论文对齐基准

本计划以论文 4.1 的以下要求作为主对齐基准：

1. **PCS 开口语义**  
   prover 先承诺 `f`，verifier 在 evaluation phase 选择 `α`，prover 返回 `v = f(α)`，再归约到虚拟 oracle  
   `g = (f - v) / (X - α)`，见 `mineru-md/Z2KSNARK/hybrid_auto/Z2KSNARK.md:526`

2. **FRI 折叠语义**  
   使用 multiplicative folding，把 `RS[L, ρ]` 的 proximity test 递归降到更小域，见  
   `mineru-md/Z2KSNARK/hybrid_auto/Z2KSNARK.md:528`  
   `mineru-md/Z2KSNARK/hybrid_auto/Z2KSNARK.md:540`

3. **PCS 证明对象语义**  
   论文 4.1 + BCS 编译对应的是“commitment + sparse openings”，而不是把整轮 oracle / 中间 witness 都显式塞进 proof，见  
   `mineru-md/Z2KSNARK/hybrid_auto/Z2KSNARK.md:554`  
   `mineru-md/Z2KSNARK/hybrid_auto/Z2KSNARK.md:556`

4. **查询点限制**  
   在 Galois ring 上，`α` 需要限制在 `T`，并且 `L ⊂ T`，以保证 `(β - α)` 可逆，见  
   `mineru-md/Z2KSNARK/hybrid_auto/Z2KSNARK.md:526`

## 3. 当前已确认的主要差距

| 编号 | 当前状态 | 与论文不一致点 | 优先级 |
| --- | --- | --- | --- |
| G1 | 当前 `FriInstance` 只有 `domain` / `claimed_degree` | 没有 `α / v / g` 的 PCS 开口语义 | P0 |
| G2 | `FriRoundProof` 仍包含整轮 `oracle_evals` | 不是 sparse-opening proof | P0 |
| G3 | `StirRoundProof` 仍包含大量中间多项式和整轮 witness | verifier 能力模型远强于论文式 opening verifier | P1 |
| G4 | verifier 依赖整轮 oracle / polynomial 重算 | 不是 “commitment + queried openings” 验证模型 | P0 |
| G5 | `serialized_bytes_actual` 现已按确定性 serializer 统计真实 external message bytes | 此项已由 Phase 4 收口；FRI 维持 opening-only bytes，STIR 计 slim external proof | DONE |
| G6 | auto queries / security bits 仍是工程启发式 | 还没有按论文 `m` 重复参数给出协议级安全口径 | P1 |
| G7 | FRI 只支持 `fold_factor ∈ {3, 9}`，STIR 固定 `9 -> 3` | 属于论文可行子集实现，而不是 theorem-level 全一般性实现 | P2 |

## 4. 完成定义

只有同时满足下面条件，才能把 “FRI PCS 与论文 4.1 对齐” 标记为完成：

- [x] 对外存在明确的 `commit` / `open` 协议表面，而不是只有 “prove polynomial is low-degree”
- [x] `open` 显式接收或派生 `α`，显式校验 `v = f(α)`
- [x] FRI 证明阶段针对的是 `g = (f-v)/(X-α)` 的 proximity，而不是直接对完整 `f` 做当前这套 heavy-witness 校验
- [x] 对外 `FriProof` 不再包含整轮 `oracle_evals`
- [x] verifier 不再依赖整轮 oracle / 中间 polynomial 的显式传输
- [x] proof-size 统计来自“真实传输 proof”的确定性序列化或固定宽度编码
- [x] README 与 benchmark 文案不再把 prototype-heavy verifier 描述成论文式 PCS verifier

## 5. 执行策略

总体采用 **“先拆 witness / proof，再补 PCS 语义，再瘦 verifier，再统一 proof size”** 的顺序。  
不要一上来直接改所有协议面；否则很容易把当前可工作的 FRI/STIR 原型一起打碎。

---

## 6. 分阶段执行计划

### Phase 0：冻结基线并补护栏

**目标**  
先把“当前实现到底依赖了哪些重 witness 字段”固化成显式测试和文档，避免后续瘦身时误删关键路径。

**任务**

- [x] 记录当前 FRI verifier 对 `oracle_evals` 的依赖点
- [x] 记录当前 STIR verifier 对 `shifted_oracle_evals`、`input_polynomial`、`folded_polynomial`、`quotient_polynomial`、`next_polynomial` 的依赖点
- [x] 给 `tests/test_fri.cpp` 和 `tests/test_stir.cpp` 增加“proof 结构瘦身前置注释 / TODO 分组”
- [x] 在 README 增加一句明确边界：当前 FRI/STIR 仍是 prototype verifier，不应表述为 theorem-4.1-complete PCS

**建议涉及文件**

- `src/fri/verifier.cpp`
- `src/stir/verifier.cpp`
- `tests/test_fri.cpp`
- `tests/test_stir.cpp`
- `README.md`

**验收标准**

- [x] 后续每删掉一个 witness 字段，都能准确知道会打断哪条 verifier 路径

---

### Phase 1：拆分“内部 witness”与“外部 proof”

**目标**  
先把当前 proof struct 的双重职责拆开：  
一类对象是 prover 内部构造时需要的完整 witness；另一类对象才是未来真正要发给 verifier 的外部 proof。

**任务**

- [x] 为 FRI 定义内部 `FriRoundWitness` / 外部 `FriRoundProof`
- [x] 为 STIR 定义内部 `StirRoundWitness` / 外部 `StirRoundProof`
- [x] 把目前仅供 prover 内部使用的字段从“对外 proof 结构体”中搬到内部 witness
- [x] 暂时允许 prover 在内部仍保留完整 oracle / polynomial，以保证重构时功能不丢
- [x] verifier 先保留旧逻辑，但改为显式从“内部兼容层”读取，而不是默认所有字段都是 public proof 字段

**FRI 预计要迁出的字段**

- [x] `oracle_evals`
- [x] 终轮整轮 evaluation table

**STIR 预计要迁出的字段**

- [x] `input_polynomial`
- [x] `folded_polynomial`
- [x] `shifted_oracle_evals`
- [x] `answer_polynomial`
- [x] `vanishing_polynomial`
- [x] `quotient_polynomial`
- [x] `next_polynomial`
- [x] 其他只为重算方便保留的中间 witness

**建议涉及文件**

- `include/fri/common.hpp`
- `src/fri/prover.cpp`
- `src/fri/verifier.cpp`
- `include/stir/common.hpp`
- `src/stir/prover.cpp`
- `src/stir/verifier.cpp`

**验收标准**

- [x] 对外 proof 结构体只包含“理论上应发送给 verifier”的字段
- [x] 内部 witness 仍能支持当前实现平滑过渡

---

### Phase 2：补上论文 4.1 的 PCS commit/open 表面

**目标**  
把当前 “prove low-degree codeword” 升级成论文 4.1 的 “commit polynomial, then open at `α`”。

**任务**

- [x] 定义 FRI PCS commitment 对象，至少包含：
  - [x] 域信息
  - [x] degree bound
  - [x] 初始 oracle root / commitment
- [x] 定义 evaluation/opening 请求对象，显式包含：
  - [x] `α`
  - [x] claimed value `v`
- [x] 实现 `α ∈ T` 与 `L ⊂ T` 的约束检查
- [x] 抽出 `f(α)` 求值路径
- [x] 实现虚拟 oracle `g(β) = (f(β)-v)/(β-α)` 的查询适配层
- [x] 明确 `ρ' = ρ - 1/|L|` 在参数层如何体现

**建议 API 形态**

- [x] `commit(instance, polynomial) -> commitment`
- [x] `open(commitment, polynomial, alpha) -> FriOpening`
- [x] `verify(commitment, alpha, value, opening) -> bool`

**Phase 2 当前落地说明**

- [x] `FriCommitment` 固定承诺 `f|L` 的单点 oracle root，不再复用 FRI 轮内 bundle 语义
- [x] `FriOpening` 只承载 `{ alpha, value, proof }`；`FriOpeningArtifact` 只保留内部 / compat 用途
- [x] `opening_instance(commitment)` 将 quotient degree bound 设为 `degree_bound - 1`
- [x] `bench_time` 的 FRI PCS 行按 `rho' = d / |L|` 计算 soundness metadata

**建议涉及文件**

- `include/fri/common.hpp`
- `include/fri/prover.hpp`
- `include/fri/verifier.hpp`
- `src/fri/prover.cpp`
- `src/fri/verifier.cpp`
- `src/fri/parameters.cpp`
- `src/domain.cpp`

**验收标准**

- [x] 调用接口已经是 “commit / open / verify”
- [x] verifier 输入中显式有 `α` 与 `v`
- [x] `α` 的合法性检查与 `β-α` 可逆性约束已落地

---

### Phase 3：把 FRI 验证模型改成 sparse-opening verifier

**目标**  
让 FRI verifier 不再依赖整轮 `oracle_evals`，而是只依赖：

- roots
- queried leaves
- sibling hashes
- final polynomial 或终轮少量 opening

**任务**

- [x] 重写 FRI prover 的每轮输出，只发送被 query 的 bundle payload + multiproof
- [x] verifier 不再重建整轮 oracle tree
- [x] verifier 只使用 queried bundle 检查 folding consistency
- [x] 终轮不再要求整轮 `oracle_evals` 显式出现在 proof 里
- [x] 终轮改为 `final_polynomial + terminal sparse opening` 收尾
- [x] 清理 `FriRoundProof` 中不应显式发送的 transcript-derived 字段

**特别注意**

- `query_positions` 和 `folding_alpha` 已从对外 `FriRoundProof` 中剥离；verifier 现阶段完全依赖 transcript 重放这些值
- PCS 首轮 quotient oracle 不再作为 committed full table 发送；verifier 通过 commitment 下的 `f(β)` sparse openings 现场计算 `g(β) = (f(β)-v)/(β-α)`

**建议涉及文件**

- `include/fri/common.hpp`
- `src/fri/prover.cpp`
- `src/fri/verifier.cpp`
- `src/crypto/merkle_tree/merkle_tree.cpp`
- `tests/test_fri.cpp`

**验收标准**

- [x] `FriProof` 不再包含整轮 `oracle_evals`
- [x] verifier 不再需要整轮表就能完成验证
- [x] honest / tamper regression 仍全部通过

---

### Phase 4：统一“真实 proof”与 proof-size 统计口径

**目标**  
把当前 `serialized_bytes_actual` 从“compact payload 统计”推进到“真实 external proof 的确定性编码长度”。

**任务**

- [x] 为外部 proof 定义固定宽度或确定性序列化格式
- [x] `serialized_bytes_actual` 改为：
  - [x] 由统一 serializer/CountingSink 统计
- [x] 删除当前所有“proof 里还在、但字节统计故意不算”的灰区
- [x] 让 `bench_time` 里的 proof-size 和真正 `FriProof` / `StirProof` 对外对象一一对应

**建议涉及文件**

- `include/ldt.hpp`
- `src/fri/prover.cpp`
- `src/stir/prover.cpp`
- `bench/bench_time.cpp`
- `README.md`

**验收标准**

- [x] `serialized_bytes_actual` 与“真正发送的 proof 对象”完全一致
- [x] 不再需要解释“verifier 用得到但这里没计费”的字段差异

---

### Phase 5：处理 STIR proof 结构的同类问题

**目标**  
STIR 虽然不等于论文 4.1 的 FRI PCS，但当前它也存在和 FRI 类似的 heavy-witness 问题；应在 FRI 路线稳定后统一处理。

**任务**

- [x] 明确 STIR 当前对外保留 `initial_root`、每轮 `g_root + betas + ans_polynomial + shake_polynomial + queries_to_prev`、以及 `queries_to_final + final_polynomial`
- [x] 把 `input_polynomial` / `folded_polynomial` / `shifted_oracle_evals` / `vanishing_polynomial` / `quotient_polynomial` / `next_polynomial` 从外部 proof 中剥离；`answer_polynomial` 改为论文式 public `ans_polynomial`
- [x] verifier 改为只依赖 queried openings、OOD answers、必要的 commitment roots、以及真正协议要求的 polynomial message
- [x] 将 `quotient_polynomial` 继续内化；`Fill_i` 不显式发送，改由 `ans_polynomial + shake_polynomial + virtual quotient + degree correction` 语义承载

**建议涉及文件**

- `include/stir/common.hpp`
- `src/stir/prover.cpp`
- `src/stir/verifier.cpp`
- `tests/test_stir.cpp`

**验收标准**

- [x] STIR proof 不再是“整轮 witness 打包”
- [x] verifier 的能力模型与对外 proof 字段一致

---

### Phase 6：用论文口径回收参数与文档

**目标**  
在协议表面对齐后，再处理“soundness 参数、文档表述、benchmark 语义”。

**任务**

- [x] 将 README 中 “prototype” 与 “paper-aligned subset” 的边界写清楚
- [x] 明确 FRI 当前支持的是论文 4.1 的哪一部分、未支持哪一部分
- [x] 审查 `engineering-heuristic-v1` 是否继续保留；如果保留，要在 benchmark 输出中明确其非论文性
- [ ] 如果未来拿到 `π_FRICom` full version，再补一轮逐步骤映射

**建议涉及文件**

- `README.md`
- `src/soundness/configurator.cpp`
- `bench/bench_time.cpp`
- `tests/test_soundness_configurator.cpp`

**验收标准**

- [x] README/bench 输出不会再让读者误以为当前实现已经 theorem-4.1-complete

---

## 7. 建议的执行顺序

推荐按下面顺序推进，不建议并行大改：

1. `Phase 0`
2. `Phase 1`
3. `Phase 2`
4. `Phase 3`
5. `Phase 4`
6. `Phase 5`
7. `Phase 6`

原因：

- `Phase 1` 不做完，后面很难安全地瘦 proof
- `Phase 2` 不做完，FRI 还不是 PCS，只是 LDT
- `Phase 3` 不做完，proof object 和 verifier 能力模型仍然不匹配
- `Phase 4` 应该在 proof shape 稳定后做，否则 proof-size 统计会重复返工
- `Phase 5` 最好复用 FRI 完成后的“proof / witness 分层”经验

## 8. 每阶段建议验证

### FRI

- [x] `cmake --build build --target test_fri bench_time`
- [x] `./build/test_fri`
- [x] `./build/bench_time --protocol fri3 --warmup 0 --reps 1 --format text`
- [x] `./build/bench_time --protocol fri9 --warmup 0 --reps 1 --format text`

### STIR

- [x] `cmake --build build --target test_stir`
- [x] `./build/test_stir`
- [x] `./build/bench_time --protocol stir9to3 --warmup 0 --reps 1 --format text`

### 文档与基准

- [x] `python3 scripts/search_benchmark_parameters.py --help`
- [x] 手工检查 README 中 proof-size / verifier model 描述是否与当前代码一致

## 9. 不在本计划首轮范围内的事项

下面这些不作为本计划首轮完成条件：

- [ ] 把 FRI 从 `3/9` 折叠推广到任意论文允许的 `s`
- [ ] WHIR 实现
- [ ] 形式化重做论文中的 soundness 证明
- [ ] 让 STIR 完全对齐论文原版全部细节

这些应等 FRI PCS 主线稳定后再另开计划。

## 10. 进度跟踪面板

| 阶段 | 状态 | 负责人 | 最近更新 | 备注 |
| --- | --- | --- | --- | --- |
| Phase 0 基线护栏 | DONE | Codex | 2026-03-09 | verifier 依赖点、tamper 护栏、README 边界已落地 |
| Phase 1 proof/witness 分层 | DONE | Codex | 2026-03-09 | external proof 已瘦身，compat carrier 保留旧 verifier 所需 witness |
| Phase 2 PCS commit/open | DONE | Codex | 2026-03-09 | FRI 已具备 `commit/open/verify` PCS 表面；`FriOpeningArtifact` 已降级为内部 / compat 层，FRI benchmark 已切到 PCS 路径 |
| Phase 3 sparse-opening FRI verifier | DONE | Codex | 2026-03-09 | public `FriOpening` / `FriProof` 已改成 sparse-opening external proof；verifier 只依赖 roots + sparse openings + `final_polynomial` |
| Phase 4 exact proof bytes | DONE | Codex | 2026-03-09 | `serialized_bytes_actual` 已切到 deterministic serializer；FRI 维持 opening-only bytes，STIR 计 slim external proof |
| Phase 5 STIR proof 瘦身 | DONE | Codex | 2026-03-09 | STIR 已迁到 route-2 proof-only verifier：public `StirProof` 保留 `initial_root`、每轮 `g_root + betas + ans_polynomial + shake_polynomial + queries_to_prev`，以及 `queries_to_final + final_polynomial` |
| Phase 6 参数与文档回收 | DONE | Codex | 2026-03-10 | README 已拆清 prototype 与 paper-aligned subset 边界；`bench_time` 现显式输出 `soundness_scope=engineering_metadata_non_paper`，soundness notes 与 benchmark 说明已明确非 theorem-level / 非 paper-complete |

## 11. 下一步建议

本计划首轮 `Phase 0-6` 已完成。

如果继续推进，建议另开新计划，处理当前已明确列为首轮范围外的事项，例如：

- `pi_FRICom` full version 拿到后的逐步骤映射
- 更一般的 FRI fold factor / 参数族支持
- `WHIR` 实现
- theorem-level soundness 证明与更完整的论文对齐
