# Archived Preset Evidence

Generated on `2026-03-07` for Batch 4 / Batch 5 tracking.

Artifacts:

- `results/preset0_size.csv`
- `results/preset0_time.csv`
- `results/preset1_size.csv`
- `results/preset1_time.csv`
- `results/preset2_size.csv`
- `results/preset2_time.csv`

Reproduction:

```bash
ctest --test-dir build --output-on-failure -R 'test_domain|test_fft3|test_folding|test_crypto|test_fri|test_stir'
./scripts/run_bench_size.sh --preset bench/presets/ci_gr216_r54.json --build-dir build --output results/preset0_size.csv
./scripts/run_bench_time.sh --preset bench/presets/ci_gr216_r54.json --build-dir build --output results/preset0_time.csv --warmup 0 --reps 1
OMP_NUM_THREADS=1 ./scripts/run_bench_size.sh --preset bench/presets/jia_micro_gr216_r162_size128.json --build-dir build-release --output results/preset1_size.csv
OMP_NUM_THREADS=1 ./scripts/run_bench_time.sh --preset bench/presets/jia_micro_gr216_r162.json --build-dir build-release --output results/preset1_time.csv --warmup 1 --reps 3
OMP_NUM_THREADS=1 ./scripts/run_bench_size.sh --preset bench/presets/scale_gr216_r486.json --build-dir build-release --output results/preset2_size.csv
OMP_NUM_THREADS=1 ./scripts/run_bench_time.sh --preset bench/presets/scale_gr216_r486.json --build-dir build-release --output results/preset2_time.csv --warmup 1 --reps 3
```

Notes:

- `preset0_*` rows use `GR(2^16,54), n=81, d=27` and contain `fri3`, `fri9`, `stir9to3`.
- `preset1_*` rows use `GR(2^16,162), n=243, d=81` and contain `fri3`, `fri9`, `stir9to3`.
- `preset2_size.csv` uses `GR(2^16,486), n=729, d=243` and contains `fri3`, `fri9`, `stir9to3`; it exercises two STIR rounds (`729 -> 243 -> 81`) in the estimator path.
- `preset2_time.csv` uses the same preset with `pow_bits = 0`, `warmup = 1`, `reps = 3`; in this measured path, `stir9to3` is still heavier than `fri9` on both `prover_total_ms` (`7370.485` vs `3930.196`) and `verify_ms` (`8240.489` vs `324.210`).
- Under the current estimator, `preset2` keeps `fri9` and `stir9to3` at the same `estimated_verifier_hashes = 14`, but `stir9to3` still has the larger `estimated_argument_bytes` (`36.570 KiB` vs `29.926 KiB`), so this preset is archived as a trend/result point rather than as first-wave time-bench headline evidence.
- `bench_time` reports measured `verifier_hashes_actual`; for `preset2`, the rows are `24 / 15 / 33` for `fri3 / fri9 / stir9to3`, so the time-side evidence is kept as a supplementary archive rather than a headline comparison.
- `scripts/run_bench_size.sh` / `scripts/run_bench_time.sh` now run `csv/json` protocol lists in one process so Batch 4 presets reuse one warmed `GRContext`.
