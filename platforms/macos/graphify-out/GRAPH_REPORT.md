# Graph Report - macos  (2026-10-25)

## Corpus Check
- Incremental progress-prose refresh only. Existing code and other semantic evidence preserved; this graph does not establish implementation, parity or release acceptance.

## Summary
- 90 nodes · 93 edges · 5 communities (4 shown, 1 thin omitted)
- Extraction: 99% EXTRACTED · 1% INFERRED · 0% AMBIGUOUS · INFERRED: 1 edges (avg confidence: 0.85)
- Token cost: unavailable (host-agent usage is not exposed; zero placeholders are not measured usage).

## Graph Freshness
- Built from commit: `48fe7713`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- Desktop implementation progress
- macOS Progress And Coverage
- macOS Graph Navigation
- macOS Native Pilot
- macOS Progress And Coverage

## God Nodes (most connected - your core abstractions)
1. `macOS progress` - 33 edges
2. `macOS Implementation Progress` - 32 edges
3. `macOS Knowledge Graph` - 10 edges
4. `macOS Scoped Agent Guidance` - 8 edges
5. `macOS Pilot Default` - 6 edges
6. `Native macOS Application` - 3 edges
7. `Regenerate All Progress Files` - 3 edges
8. `Refresh Graph After Code` - 2 edges
9. `Dirty Graph Remains Usable` - 2 edges
10. `Query Graph Before Source` - 2 edges

## Surprising Connections (you probably didn't know these)
- `Native macOS Application` --conceptually_related_to--> `macOS ImageEditor Module`  [INFERRED]
  README.md → image-editor/README.md
- `Implementation Progress Report` --references--> `macOS Implementation Progress`  [EXTRACTED]
  README.md → PROGRESS.md

## Communities (5 total, 1 thin omitted)

### Community 0 - "Desktop implementation progress"
Cohesion: 0.06
Nodes (25): python tools/platform-progress/update.py --check verifies generated desktop progress reports, COMPATIBILITY-CATALOG-001 | 0.0% | 0 | 0 | 0 | 18 | No platform trace map, CORE-PLATFORM-001 | 0.0% | 0 | 0 | 0 | 36 | No platform trace map, macOS: 0 implemented, 0 partial, 0 not started, 324 without a recorded implementation status. Native platform evidence., macOS progress, EDITOR-ANNOTATIONS-001 | 0.0% | 0 | 0 | 0 | 20 | No platform trace map, EDITOR-CANVAS-001 | 0.0% | 0 | 0 | 0 | 17 | No platform trace map, EDITOR-EFFECTS-001 | 0.0% | 0 | 0 | 0 | 18 | No platform trace map (+17 more)

### Community 1 - "macOS Progress And Coverage"
Cohesion: 0.06
Nodes (21): COMPATIBILITY-CATALOG-001, Product Contract 0.3.1, CORE-PLATFORM-001, EDITOR-ANNOTATIONS-001, EDITOR-CANVAS-001, EDITOR-EFFECTS-001, EDITOR-OUTPUT-ACTIONS-001, EDITOR-SESSION-001 (+13 more)

### Community 2 - "macOS Graph Navigation"
Cohesion: 0.26
Nodes (5): Graphify Explain, Graphify Path, Graphify Query, macOS Knowledge Graph, macOS Scoped Agent Guidance

### Community 3 - "macOS Native Pilot"
Cohesion: 0.20
Nodes (8): macOS ImageEditor Module, AppKit Native Surfaces, Native macOS Application, Implementation Progress Report, macOS 14 Sonoma Minimum, D-MAC-001, Swift Language, SwiftUI Chrome

## Knowledge Gaps
- **54 isolated node(s):** `Graphify Explain`, `Graphify Path`, `Graphify Query`, `AppKit Native Surfaces`, `macOS 14 Sonoma Minimum` (+49 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 75 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `macOS Implementation Progress` connect `macOS Progress And Coverage` to `macOS Native Pilot`, `macOS Progress And Coverage`?**
  _High betweenness centrality (0.218) - this node is a cross-community bridge._
- **What connects `Graphify Explain`, `Graphify Path`, `Graphify Query` to the rest of the system?**
  _54 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `Desktop implementation progress` be split into smaller, more focused modules?**
  _Cohesion score 0.058823529411764705 - nodes in this community are weakly interconnected._
- **Why does `Implementation Progress Report` connect `macOS Native Pilot` to `macOS Progress And Coverage`?**
  _High betweenness centrality (0.078) - this node is a cross-community bridge._
- **Should `macOS Progress And Coverage` be split into smaller, more focused modules?**
  _Cohesion score 0.06451612903225806 - nodes in this community are weakly interconnected._