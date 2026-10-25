# Graph Report - macos  (2026-10-25)

## Corpus Check
- Corpus is ~1,048 words - fits in a single context window. You may not need a graph.

## Summary
- 56 nodes · 60 edges · 4 communities (3 shown, 1 thin omitted)
- Extraction: 98% EXTRACTED · 2% INFERRED · 0% AMBIGUOUS · INFERRED: 1 edges (avg confidence: 0.85)
- Token cost: unavailable for host-agent semantic extraction (zero stored counters are placeholders).

## Community Hubs (Navigation)
- macOS Progress And Coverage
- macOS Graph Navigation
- macOS Native Pilot
- macOS Progress And Coverage

## God Nodes (most connected - your core abstractions)
1. `macOS Implementation Progress` - 32 edges
2. `macOS Knowledge Graph` - 10 edges
3. `macOS Scoped Agent Guidance` - 8 edges
4. `macOS Pilot Default` - 6 edges
5. `Native macOS Application` - 3 edges
6. `Regenerate All Progress Files` - 3 edges
7. `Query Graph Before Source` - 2 edges
8. `Dirty Graph Remains Usable` - 2 edges
9. `Graph Wiki Navigation` - 2 edges
10. `Report For Broad Architecture` - 2 edges

## Surprising Connections (you probably didn't know these)
- `Native macOS Application` --conceptually_related_to--> `macOS ImageEditor Module`  [INFERRED]
  README.md → image-editor/README.md
- `Implementation Progress Report` --references--> `macOS Implementation Progress`  [EXTRACTED]
  README.md → PROGRESS.md

## Communities (4 total, 1 thin omitted)

### Community 0 - "macOS Progress And Coverage"
Cohesion: 0.06
Nodes (21): COMPATIBILITY-CATALOG-001, Product Contract 0.3.1, CORE-PLATFORM-001, EDITOR-ANNOTATIONS-001, EDITOR-CANVAS-001, EDITOR-EFFECTS-001, EDITOR-OUTPUT-ACTIONS-001, EDITOR-SESSION-001 (+13 more)

### Community 1 - "macOS Graph Navigation"
Cohesion: 0.26
Nodes (5): Graphify Explain, Graphify Path, Graphify Query, macOS Knowledge Graph, macOS Scoped Agent Guidance

### Community 2 - "macOS Native Pilot"
Cohesion: 0.20
Nodes (8): macOS ImageEditor Module, AppKit Native Surfaces, Native macOS Application, Implementation Progress Report, macOS 14 Sonoma Minimum, D-MAC-001, Swift Language, SwiftUI Chrome

## Knowledge Gaps
- **30 isolated node(s):** `Graphify Query`, `Graphify Path`, `Graphify Explain`, `AppKit Native Surfaces`, `macOS 14 Sonoma Minimum` (+25 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 42 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `macOS Implementation Progress` connect `macOS Progress And Coverage` to `macOS Native Pilot`, `macOS Progress And Coverage`?**
  _High betweenness centrality (0.576) - this node is a cross-community bridge._
- **What connects `Graphify Query`, `Graphify Path`, `Graphify Explain` to the rest of the system?**
  _30 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `macOS Progress And Coverage` be split into smaller, more focused modules?**
  _Cohesion score 0.06451612903225806 - nodes in this community are weakly interconnected._
- **Why does `Implementation Progress Report` connect `macOS Native Pilot` to `macOS Progress And Coverage`?**
  _High betweenness centrality (0.206) - this node is a cross-community bridge._

## Scoped Semantic Refresh

- Fully reread README.md and PROGRESS.md; reused unchanged AGENTS.md and ImageEditor README only after source/prompt hash validation.
- Percentages are unweighted recorded implementation coverage, not release approval: this platform 0.0%, Linux 30.6%, overall desktop 10.2%. Missing maps leave 324 requirements unassessed.
- EXTRACTED edges reflect explicit source relationships. INFERRED edges require source verification before correctness or parity claims.
- Actual host-agent token usage is unavailable. No native application build or qualification is claimed.
