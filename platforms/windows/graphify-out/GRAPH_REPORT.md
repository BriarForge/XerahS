# Graph Report - windows  (2026-10-25)

## Corpus Check
- Corpus is ~1,055 words - fits in a single context window. You may not need a graph.

## Summary
- 61 nodes · 65 edges · 5 communities (4 shown, 1 thin omitted)
- Extraction: 98% EXTRACTED · 2% INFERRED · 0% AMBIGUOUS · INFERRED: 1 edges (avg confidence: 0.85)
- Token cost: unavailable for host-agent semantic extraction (zero stored counters are placeholders).

## Community Hubs (Navigation)
- Windows Progress And Coverage
- Windows Graph Navigation
- Windows Native Pilot
- Windows Native Guidance
- Windows Progress And Coverage

## God Nodes (most connected - your core abstractions)
1. `Windows Implementation Progress` - 32 edges
2. `Windows Pilot Default` - 11 edges
3. `Windows Knowledge Graph` - 10 edges
4. `Windows Scoped Agent Guidance` - 8 edges
5. `Native Windows Application` - 3 edges
6. `Regenerate All Progress Files` - 3 edges
7. `Query Graph Before Source` - 2 edges
8. `Dirty Graph Remains Usable` - 2 edges
9. `Graph Wiki Navigation` - 2 edges
10. `Report For Broad Architecture` - 2 edges

## Surprising Connections (you probably didn't know these)
- `Native Windows Application` --conceptually_related_to--> `Windows ImageEditor Module`  [INFERRED]
  README.md → image-editor/README.md
- `Implementation Progress Report` --references--> `Windows Implementation Progress`  [EXTRACTED]
  README.md → PROGRESS.md

## Communities (5 total, 1 thin omitted)

### Community 0 - "Windows Progress And Coverage"
Cohesion: 0.06
Nodes (21): COMPATIBILITY-CATALOG-001, Product Contract 0.3.1, CORE-PLATFORM-001, EDITOR-ANNOTATIONS-001, EDITOR-CANVAS-001, EDITOR-EFFECTS-001, EDITOR-OUTPUT-ACTIONS-001, EDITOR-SESSION-001 (+13 more)

### Community 1 - "Windows Graph Navigation"
Cohesion: 0.26
Nodes (5): Graphify Explain, Graphify Path, Graphify Query, Windows Knowledge Graph, Windows Scoped Agent Guidance

### Community 2 - "Windows Native Pilot"
Cohesion: 0.18
Nodes (10): C# Language, DXGI Capture, Native Hotkeys, Windows 11 23H2 Minimum, D-WIN-001, Native Tray, Unpackaged Desktop First, Win32 WinRT Native Surfaces (+2 more)

### Community 3 - "Windows Native Guidance"
Cohesion: 0.50
Nodes (3): Windows ImageEditor Module, Native Windows Application, Implementation Progress Report

## Knowledge Gaps
- **35 isolated node(s):** `Graphify Query`, `Graphify Path`, `Graphify Explain`, `C# Language`, `DXGI Capture` (+30 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 47 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Windows Implementation Progress` connect `Windows Progress And Coverage` to `Windows Native Guidance`, `Windows Progress And Coverage`?**
  _High betweenness centrality (0.576) - this node is a cross-community bridge._
- **What connects `Graphify Query`, `Graphify Path`, `Graphify Explain` to the rest of the system?**
  _35 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Windows Progress And Coverage` be split into smaller, more focused modules?**
  _Cohesion score 0.06451612903225806 - nodes in this community are weakly interconnected._
- **Why does `Native Windows Application` connect `Windows Native Guidance` to `Windows Native Pilot`?**
  _High betweenness centrality (0.269) - this node is a cross-community bridge._
- **Why does `Implementation Progress Report` connect `Windows Native Guidance` to `Windows Progress And Coverage`?**
  _High betweenness centrality (0.269) - this node is a cross-community bridge._

## Scoped Semantic Refresh

- Fully reread README.md and PROGRESS.md; reused unchanged AGENTS.md and ImageEditor README only after source/prompt hash validation.
- Percentages are unweighted recorded implementation coverage, not release approval: this platform 0.0%, Linux 30.6%, overall desktop 10.2%. Missing maps leave 324 requirements unassessed.
- EXTRACTED edges reflect explicit source relationships. INFERRED edges require source verification before correctness or parity claims.
- Actual host-agent token usage is unavailable. No native application build or qualification is claimed.
