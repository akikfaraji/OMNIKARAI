# AI Ecosystem (PARTIAL today → PLANNED stack)

> Omnikarai's long-term AI story: a **native** stack — tensors, layers,
> training, inference, quantization, serving — compiled to the same
> machine code as everything else. Python/PyTorch/NumPy are comparison
> and benchmark targets, not execution substrates ([VISION.md](VISION.md)).
> The numerical substrate under everything here is Namurai
> ([NAMURAI.md](NAMURAI.md)).

## What exists today (PARTIAL — the seed)

The built-in `ai` module ([MODULES.md](MODULES.md)) provides:

- `ai.alloc(n)` / `ai.free(buf)` — 64-byte-aligned, zeroed buffers
- `ai.set/get` (FP32 bit patterns), `ai.set_u8/get_u8/set_i8/get_i8` (bytes)
- `ai.dot` (FP32, VFMADD231PS, 8 floats/cycle), `ai.dot_i8` (INT8,
  VPMADDUBSW, 32 ops/cycle)
- `ai.matmul` (cache-tiled matrix × vector), `ai.relu` (VMAXPS),
  `ai.softmax` (fused AVX2 exp)
- scalar fallbacks for non-AVX2 hosts (`make portable`)

Evidence: tests t16–t20 (30/30 green), CI lanes. The MLP benchmark seed
(`t_bench_mlp`) demonstrates a small network composed from these kernels.

Honesty note: this is a kernel library, **not** an AI ecosystem. Nothing
below exists yet.

## Target module map (conceptual names — final names fixed by naming review)

| Module | Responsibility | Stage |
|--------|----------------|-------|
| `ai` (built-in) | raw buffers + SIMD kernels | **TODAY** |
| `ai.tensor` | Namurai-backed tensors, autodiff-lite graph | V01.08 |
| `ai.nn` | layers: dense, conv (later), norm, dropout | V01.08 |
| `ai.optim` | SGD, momentum, Adam-family | V01.08 |
| `ai.loss` | MSE, cross-entropy, L1 | V01.08 |
| `ai.training` | loops, batching, checkpointing, LR schedules | V01.08 |
| `ai.inference` | load-and-run, session model | V01.08 |
| `ai.dataset` / `ai.data` | dataset abstraction, loaders, preprocessing | V01.08 |
| `ai.quant` | beyond INT8 dot: calibration, quantized layers | V01.08 |
| `ai.serialization` | versioned model format, checksummed | V01.08 |
| `ai.metrics` | accuracy, perplexity, custom reductions | V01.08 |
| `ai.distributed` | multi-node data/model parallelism | EXPLORATORY ([FUTURE.md](FUTURE.md)) |
| `ai.runtime` / serving | persistent serving processes, warm models | EXPLORATORY |
| `ai.model` | registry of reference model definitions | EXPLORATORY |

**Naming rule:** these are conceptual placeholders. Final names are decided
by a naming review against the real grammar and the existing built-in `ai`
module at the V01.08 PROPOSAL gate ([PROJECT_DISCIPLINE.md](PROJECT_DISCIPLINE.md))
— the built-in `ai` keeps its current API regardless (compatibility).

## Capability coverage (what the stack must eventually do)

- **Tensors**: creation, views, ops on Namurai arrays.
- **Model representation**: explicit layer graphs — inspectable, printable,
  serializable; no hidden global state (AI agents must be able to *read*
  a model definition — [AI_NATIVE_DESIGN.md](AI_NATIVE_DESIGN.md)).
- **Training**: deterministic-order loops by default; checkpoint/resume;
  seeded RNG for reproducibility.
- **Inference**: load serialized models, run with warm buffers; startup
  time is a tracked benchmark metric.
- **Quantization**: INT8 first (kernels exist today), calibration flow,
  quality-comparison tests vs FP32.
- **CPU acceleration**: SIMD per tier; W^X JIT for dynamically fused
  sequences (EXPLORATORY); memory pools for activation buffers.
- **Parallelism**: single-process multi-core is EXPLORATORY until the
  concurrency design lands; the V01 stack is single-threaded and honest
  about it.
- **Distributed**: EXPLORATORY, depends on the distributed runtime idea in
  [FUTURE.md](FUTURE.md).
- **Serving**: EXPLORATORY.
- **Evaluation & profiling**: metrics module + compiler profiling hooks
  (V01.11 benchmark-lab work feeds this).

## Delivery order (roadmap-aligned)

V01.07 Namurai → V01.08 the stack above (CPU/SIMD scope) → V01.09 MCP
tooling on top ([MCP.md](MCP.md)). Release criteria live in
[ROADMAP.md](ROADMAP.md); the flagship V01.08 criterion: *train a small
network from Omnikarai source, quantize it, run inference — all native,
benchmarked, no Python involved.*
