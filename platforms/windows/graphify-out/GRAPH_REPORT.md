# Graph Report - windows  (2026-10-25)

## Corpus Check
- Incremental progress-prose refresh only. Existing code and other semantic evidence preserved; this graph does not establish implementation, parity or release acceptance.

## Summary
- 95 nodes · 98 edges · 5 communities (4 shown, 1 thin omitted)
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 1 edges (avg confidence: 0.85)
- Token cost: unavailable (host-agent usage is not exposed; zero placeholders are not measured usage).

## Graph Freshness
- Built from commit: `48fe7713`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- Desktop implementation progress
- Windows Progress And Coverage
- Windows Native Pilot
- Windows Graph Navigation
- Windows Progress And Coverage

## God Nodes (most connected - your core abstractions)
1. `Windows progress` - 33 edges
2. `Windows Implementation Progress` - 32 edges
3. `Windows Pilot Default` - 11 edges
4. `Windows Knowledge Graph` - 10 edges
5. `Windows Scoped Agent Guidance` - 8 edges
6. `Native Windows Application` - 3 edges
7. `Regenerate All Progress Files` - 3 edges
8. `Refresh Graph After Code` - 2 edges
9. `Dirty Graph Remains Usable` - 2 edges
10. `Query Graph Before Source` - 2 edges

## Surprising Connections (you probably didn't know these)
- `Native Windows Application` --conceptually_related_to--> `Windows ImageEditor Module`  [INFERRED]
  README.md → image-editor/README.md
- `Implementation Progress Report` --references--> `Windows Implementation Progress`  [EXTRACTED]
  README.md → PROGRESS.md

## Communities (5 total, 1 thin omitted)

### Community 0 - "Desktop implementation progress"
Cohesion: 0.06
Nodes (25): python tools/platform-progress/update.py --check verifies generated desktop progress reports, COMPATIBILITY-CATALOG-001 | 0.0% | 0 | 0 | 0 | 18 | No platform trace map, CORE-PLATFORM-001 | 0.0% | 0 | 0 | 0 | 36 | No platform trace map, Windows: 0 implemented, 0 partial, 0 not started, 324 without a recorded implementation status. Native platform evidence., Windows progress, EDITOR-ANNOTATIONS-001 | 0.0% | 0 | 0 | 0 | 20 | No platform trace map, EDITOR-CANVAS-001 | 0.0% | 0 | 0 | 0 | 17 | No platform trace map, EDITOR-EFFECTS-001 | 0.0% | 0 | 0 | 0 | 18 | No platform trace map (+17 more)

### Community 1 - "Windows Progress And Coverage"
Cohesion: 0.06
Nodes (21): COMPATIBILITY-CATALOG-001, Product Contract 0.3.1, CORE-PLATFORM-001, EDITOR-ANNOTATIONS-001, EDITOR-CANVAS-001, EDITOR-EFFECTS-001, EDITOR-OUTPUT-ACTIONS-001, EDITOR-SESSION-001 (+13 more)

### Community 2 - "Windows Native Pilot"
Cohesion: 0.13
Nodes (13): Windows ImageEditor Module, Native Windows Application, C# Language, DXGI Capture, Native Hotkeys, Implementation Progress Report, Windows 11 23H2 Minimum, D-WIN-001 (+5 more)

### Community 3 - "Windows Graph Navigation"
Cohesion: 0.26
Nodes (5): Graphify Explain, Graphify Path, Graphify Query, Windows Knowledge Graph, Windows Scoped Agent Guidance

## Knowledge Gaps
- **59 isolated node(s):** `Graphify Explain`, `Graphify Path`, `Graphify Query`, `C# Language`, `DXGI Capture` (+54 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 80 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Windows Implementation Progress` connect `Windows Progress And Coverage` to `Windows Native Pilot`, `Windows Progress And Coverage`?**
  _High betweenness centrality (0.233) - this node is a cross-community bridge._
- **What connects `Graphify Explain`, `Graphify Path`, `Graphify Query` to the rest of the system?**
  _59 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Desktop implementation progress` be split into smaller, more focused modules?**
  _Cohesion score 0.058823529411764705 - nodes in this community are weakly interconnected._
- **Should `Windows Progress And Coverage` be split into smaller, more focused modules?**
  _Cohesion score 0.06451612903225806 - nodes in this community are weakly interconnected._
- **Should `Windows Native Pilot` be split into smaller, more focused modules?**
  _Cohesion score 0.13333333333333333 - nodes in this community are weakly interconnected._