# Benchmarks — the Benchmark Lab

> Purpose: turn performance claims from UNPROVEN into BENCHMARKED —
> honestly, reproducibly, and without cherry-picking. The performance
> principle and its four categories (CURRENT / TARGET / BENCHMARKED /
> UNPROVEN) are defined in [VISION.md](VISION.md). Status today: a working
> reproducible runner exists; the full metric lab is roadmap V01.11
> (binary-size/startup metrics arrive with V01.02's native emitters).

## What exists today (TODAY)

- Multi-language benchmark programs in `benchmarks/`: **Omnikarai vs C,
  C++, Go, Java, JavaScript, Python** for fib, loops, primes, matmul, dot
  products (and the AI kernel benchmarks: dot FP32/INT8, matmul, softmax).
- Reproducible runner: `python3 benchmarks/run_benchmarks.py` — measures
  and prints what it measures, so results are always accompanied by their
  conditions.
- Project rule in force: **no claimed speedups are published without the
  runner output next to them.**

Known limitation: today's runner measures wall-clock time only. That is
why no memory-efficiency claims appear anywhere in this documentation set
([TECHNICAL_DEBT.md](TECHNICAL_DEBT.md) TD-11).

## Target ecosystems (comparisons are against *appropriate* implementations)

C · C++ · Rust · Python · NumPy · Julia (where relevant) · PyTorch (where
relevant) · other appropriate ecosystems (Go/Java/JS already present).
Equivalence rule: benchmarks compare **equivalent workloads** — same
algorithm, same input size, equivalent optimization effort (e.g. `-O2`/
`-O3` C vs omnicc default; `-C opt-level=3` Rust) — never artificial
asymmetric examples, never debug-vs-release mismatches.

## Categories and workload list (target state)

### General computing
loops · arithmetic · recursion (fib, primes) · sorting · hashing ·
filesystem I/O · networking I/O · concurrency (post-concurrency-design)

### Memory
allocation · deallocation · large buffers · random access · sequential
access · memory bandwidth

### Numerical
vector operations · matrix multiplication · reductions · FFT · linear
algebra (decompositions, solvers) — largely owned by Namurai
([NAMURAI.md](NAMURAI.md))

### AI
tensor operations · inference · quantization (INT8 first — kernels exist
today) · matrix kernels · small neural networks · transformer primitives
(grows out of `t_bench_mlp`, [AI_ECOSYSTEM.md](AI_ECOSYSTEM.md))

## Metrics (target state — every benchmark records all of these)

| Metric | Notes |
|--------|-------|
| runtime | median + spread over fixed iterations, warmup defined |
| peak memory | RSS peak |
| allocation count | requires the V01.01 allocator hooks |
| startup time | process start → first output (meaningful once V01.02 gives real binaries) |
| compilation time | omnicc compile time per workload |
| binary size | real once V01.02 replaces engine-embedded builds |
| throughput / latency | workload-dependent (items/sec; p50/p99) |
| SIMD utilization | kernel-tier report (AVX2/NEON/scalar path taken) |
| CPU utilization | user/sys split |
| AI workload performance | tokens/s-equivalent, layer timings |
| scaling behavior | input-size sweeps, documented curve |

### Environment metadata (mandatory per result)

language · compiler · compiler version · optimization flags · CPU model ·
RAM · OS · architecture · runtime version · workload name · input size ·
plus every metric above. No metadata, no published result.

## Honesty rules (binding)

1. **Never cherry-pick.** All results for a category are published
   together, or none are. Selecting the flattering subset is misconduct.
2. **Runner output or it didn't happen.** Claims cite a result file with
   full metadata.
3. **Equivalent workloads only** (equivalence rule above).
4. **Reproducibility beats drama.** Any result must be re-runnable from
   the repo by a stranger on their own hardware.
5. **UNPROVEN stays labeled.** Absence of a metric means absence of the
   claim ([VISION.md](VISION.md)).
6. **Regression tracking.** V01.11 adds CI perf-regression baselines;
   a silent regression is treated like a silent wrongness bug.
