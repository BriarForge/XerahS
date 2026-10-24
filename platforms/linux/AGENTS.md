# Scope
Applies to: platforms/linux/**
Parent: ../AGENTS.md

# Purpose
Owns the native Linux editions under D-LIN-001 and D-LIN-002. `linux-qt` is the supported reference edition.

# Local Rules
- **LINUX-TOOLCHAIN-001** `linux-qt` builds with CMake 3.21 or later, a C++17 compiler, and Qt 6.4 or later, dynamically linked (ROOT-LICENSE-001). Qt 6.4.2 on Ubuntu 24.04 LTS is the minimum tested baseline; raising the minimum is a recorded toolchain change in this file.
- **LINUX-CORE-001** Product logic that has a conformance seam lives in `linux-qt/src/` libraries that depend only on Qt Core, so the adapter and the application share one production code path.
- **LINUX-ADAPTER-001** `linux-qt/conformance-adapter/` is the Linux adapter for the shared runner. It only translates JSON to production calls and injected inputs; it holds no product logic.
- **LINUX-TRACE-001** Each implemented capability has `linux-qt/traceability/<CAPABILITY>.json` (PLATFORM-TRACE-001), and its ID is added to `XERAHS_IMPLEMENTED_CAPABILITIES` in `linux-qt/CMakeLists.txt` in the same change.

# Verification
From `platforms/linux/linux-qt`:

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build --output-on-failure
```

# Prohibited Changes
- Static linking of Qt.
- Product logic inside the conformance adapter.

# Escalation
Toolkit or edition changes follow D-LIN-002 and need the product owner.

# Child Scopes
None.

## graphify

This project has a knowledge graph at graphify-out/ with god nodes, community structure, and cross-file relationships.

When the user types `/graphify`, use the installed graphify skill or instructions before doing anything else.

Rules:
- When graphify-out/graph.json exists, run `graphify query "<question>"` before your first grep, search or source-file read for a codebase question, and put this rule in every subagent prompt that explores code. Read raw files only for lines the graph or an earlier read already located. Use `graphify path "<A>" "<B>"` for relationships and `graphify explain "<concept>"` for focused concepts. These return a scoped subgraph, usually much smaller than GRAPH_REPORT.md or raw grep output.
- Dirty graphify-out/ files are expected after hooks or incremental updates; dirty graph files are not a reason to skip graphify. Only skip graphify if the task is about stale or incorrect graph output, or the user explicitly says not to use it.
- If graphify-out/wiki/index.md exists, use it for broad navigation instead of raw source browsing.
- Read graphify-out/GRAPH_REPORT.md only for broad architecture review or when query/path/explain do not surface enough context.
- After modifying code, run `graphify update .` to keep the graph current (AST-only, no API cost).
- A committed graphify-out/graph.json is rewritten whole on every build, so a large diff to it in a pull request is expected. Do not flag it as churn or suggest skipping the graph rebuild or its hooks.
