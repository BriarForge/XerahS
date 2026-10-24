# Graph Report - windows  (2026-10-25)

## Corpus Check
- Corpus is ~338 words - fits in a single context window. You may not need a graph.

## Summary
- 26 nodes · 30 edges · 4 communities (2 shown, 2 thin omitted)
- Extraction: 97% EXTRACTED · 3% INFERRED · 0% AMBIGUOUS · INFERRED: 1 edges (avg confidence: 0.85)
- Token cost: unavailable for host-agent semantic extraction (stored counters are placeholders).

## Community Hubs (Navigation)
- Windows Native Pilot
- Windows Agent Guidance
- Windows Graph Navigation
- Windows Native ImageEditor

## God Nodes (most connected - your core abstractions)
1. `Windows Pilot Default` - 11 edges
2. `Windows Knowledge Graph` - 10 edges
3. `Windows Scoped Agent Guidance` - 8 edges
4. `Native Windows Application` - 2 edges
5. `Windows ImageEditor Module` - 2 edges
6. `Query Graph Before Source` - 2 edges
7. `Dirty Graph Remains Usable` - 2 edges
8. `Graph Wiki Navigation` - 2 edges
9. `Report For Broad Architecture` - 2 edges
10. `Refresh Graph After Code` - 2 edges

## Surprising Connections (you probably didn't know these)
- `Native Windows Application` --conceptually_related_to--> `Windows ImageEditor Module`  [INFERRED]
  README.md → image-editor/README.md

## Communities (4 total, 2 thin omitted)

### Community 0 - "Windows Native Pilot"
Cohesion: 0.18
Nodes (10): C# Language, DXGI Capture, Native Hotkeys, Windows 11 23H2 Minimum, D-WIN-001, Native Tray, Unpackaged Desktop First, Win32 WinRT Native Surfaces (+2 more)

### Community 2 - "Windows Graph Navigation"
Cohesion: 0.33
Nodes (4): Graphify Explain, Graphify Path, Graphify Query, Windows Knowledge Graph

## Knowledge Gaps
- **13 isolated node(s):** `D-WIN-001`, `WinUI 3 Chrome`, `Windows App SDK`, `Win32 WinRT Native Surfaces`, `DXGI Capture` (+8 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 15 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **2 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Windows Pilot Default` connect `Windows Native Pilot` to `Windows Native ImageEditor`?**
  _High betweenness centrality (0.250) - this node is a cross-community bridge._
- **What connects `D-WIN-001`, `WinUI 3 Chrome`, `Windows App SDK` to the rest of the system?**
  _13 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Why does `Windows Knowledge Graph` connect `Windows Graph Navigation` to `Windows Agent Guidance`?**
  _High betweenness centrality (0.115) - this node is a cross-community bridge._
- **Why does `Native Windows Application` connect `Windows Native ImageEditor` to `Windows Native Pilot`?**
  _High betweenness centrality (0.073) - this node is a cross-community bridge._

## Build Coverage

- This is a documentation-only native platform stub. The application and ImageEditor implementation have not started.
- Every detected document was read completely; semantic source locations and per-file coverage were verified.
- EXTRACTED edges reflect explicit statements. The application-to-ImageEditor association is INFERRED from their shared platform ownership.
- Codex hook configuration is detected as code; it is not a product implementation.
- Host-agent token usage is unavailable; zero persisted counters are placeholders.

## Scoped Build Verification

- Independent scan root: `platforms/windows`. Semantic files were fully read, or reused only after source-content and extraction-prompt hash validation.
- Verified a foreground `graphify update .` preserves semantic nodes; code-only updates do not semantically re-extract prose. All scoped manifests are current.
- Host-agent token usage is unavailable; zero stored counters do not indicate zero semantic processing cost.
- Cite EXTRACTED versus INFERRED relationships; verify inferred links against source before making correctness or parity claims.
