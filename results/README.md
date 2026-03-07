# Archived Preset Evidence

Generated on `2026-03-07` for Batch 4 closure.

Artifacts:

- `results/preset0_size.csv`
- `results/preset0_time.csv`
- `results/preset1_size.csv`
- `results/preset1_time.csv`

Reproduction:

```bash
ctest --test-dir build --output-on-failure -R 'test_domain|test_fft3|test_folding|test_crypto|test_fri|test_stir'
./scripts/run_bench_size.sh --preset bench/presets/ci_gr216_r54.json --build-dir build --output results/preset0_size.csv
./scripts/run_bench_time.sh --preset bench/presets/ci_gr216_r54.json --build-dir build --output results/preset0_time.csv --warmup 0 --reps 1
OMP_NUM_THREADS=1 ./scripts/run_bench_size.sh --preset bench/presets/jia_micro_gr216_r162_size128.json --build-dir build-release --output results/preset1_size.csv
OMP_NUM_THREADS=1 ./scripts/run_bench_time.sh --preset bench/presets/jia_micro_gr216_r162.json --build-dir build-release --output results/preset1_time.csv --warmup 1 --reps 3
```

Notes:

- `preset0_*` rows use `GR(2^16,54), n=81, d=27` and contain `fri3`, `fri9`, `stir9to3`.
- `preset1_*` rows use `GR(2^16,162), n=243, d=81` and contain `fri3`, `fri9`, `stir9to3`.
- `scripts/run_bench_size.sh` / `scripts/run_bench_time.sh` now run `csv/json` protocol lists in one process so Batch 4 presets reuse one warmed `GRContext`.
