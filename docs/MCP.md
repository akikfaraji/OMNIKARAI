# MCP & Agent Ecosystem (PLANNED — V01.09)

> Status: **PLANNED**. Nothing in this document is implemented. This page
> records the design intent for MCP (Model Context Protocol) workloads and
> AI-agent support in Omnikarai, so that compiler and runtime decisions
> made *earlier* (V01.00–V01.08) do not foreclose it. Roadmap placement:
> [ROADMAP.md](ROADMAP.md) V01.09. Related: [AI_ECOSYSTEM.md](AI_ECOSYSTEM.md),
> [AI_NATIVE_DESIGN.md](AI_NATIVE_DESIGN.md).

## Why Omnikarai for MCP/agents (design rationale)

- **Native inference**: agents whose tool implementations and even model
  serving run as native code ([AI_ECOSYSTEM.md](AI_ECOSYSTEM.md)).
- **Inspectable programs**: MCP servers are small, readable `.ok` programs
  — exactly the kind of code an AI agent can audit before wiring into a
  host.
- **Structured diagnostics** (V01.00/V01.09): when an agent writes an MCP
  server, the compiler's machine-readable errors close the loop without
  human babysitting.

## Planned package surface (conceptual names, subject to naming review)

| Area | Contents |
|------|----------|
| MCP client | connect to MCP servers over stdio and HTTP transports; capability negotiation; request/response + notifications |
| MCP server | expose **tools**, **resources**, **prompts** from an Omnikarai program; transport handled by the package |
| Tools | typed tool definitions, structured (schema-described) tool calls, structured output — errors as values, not stderr noise |
| Resources | URI-addressable resources served from Omnikarai code or files |
| Prompts | parameterized prompt templates as first-class artifacts |
| Agent loops | a minimal, inspectable agent loop: model call → tool calls → observation → repeat, with explicit context handling and stop conditions |
| Context handling | explicit context-window management primitives (truncation, summarization hooks, memory windows) |
| Model providers | provider-agnostic interface; **model routing** (which provider/model serves which call); streaming responses |
| Structured output | schema-constrained outputs (verify, not trust) |
| Function calling | native mapping between Omnikarai functions and model-callable tools |
| Embeddings | embedding generation via pluggable providers + local kernels |
| Vector search | in-process vector store: cosine/dot search over embedding buffers (Namurai-backed) |
| Agent memory | pluggable memory (volatile, file-backed, vector-backed) with explicit lifetimes ([MEMORY_MODEL.md](MEMORY_MODEL.md)) |

## Language/tooling support this depends on

| Dependency | Provided by | Why it matters for agents |
|------------|-------------|---------------------------|
| `check --json` diagnostics | V01.00 (v0) / V01.09 (GA) | agents compile-check without parsing stderr |
| AST / semantic-graph export | V01.09 | agents analyze programs instead of guessing |
| deterministic builds | V01.02 groundwork | an agent's edit produces a predictable artifact |
| benchmark interface | V01.11 | agents measure their own optimizations |
| structured tests | existing runner | agents verify behavior without human judgment |

## Explicit non-goals for V01.09

- No hosted/proprietary agent service — the packages are libraries; where
  commercial tiers appear, they are documented in
  [GOVERNANCE.md](GOVERNANCE.md) without commitments.
- No "agent framework" lock-in: the loop is ~100 lines of Omnikarai the
  user can read, fork, or replace.
- No claim of agent safety: agents execute tools with user-granted
  permissions; capability labeling follows
  [PACKAGE_SECURITY.md](PACKAGE_SECURITY.md).
