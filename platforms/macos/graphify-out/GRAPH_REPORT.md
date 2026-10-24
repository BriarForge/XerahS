# Graph Report - macos  (2026-10-25)

## Corpus Check
- Corpus is ~331 words - fits in a single context window. You may not need a graph.

## Summary
- 21 nodes · 25 edges · 4 communities (2 shown, 2 thin omitted)
- Extraction: 96% EXTRACTED · 4% INFERRED · 0% AMBIGUOUS · INFERRED: 1 edges (avg confidence: 0.85)
- Token cost: unavailable for host-agent semantic extraction (stored counters are placeholders).

## Community Hubs (Navigation)
- macOS Agent Guidance
- macOS Graph Navigation
- macOS Native Pilot
- macOS Native ImageEditor

## God Nodes (most connected - your core abstractions)
1. `macOS Knowledge Graph` - 10 edges
2. `macOS Scoped Agent Guidance` - 8 edges
3. `macOS Pilot Default` - 6 edges
4. `Native macOS Application` - 2 edges
5. `macOS ImageEditor Module` - 2 edges
6. `Query Graph Before Source` - 2 edges
7. `Dirty Graph Remains Usable` - 2 edges
8. `Graph Wiki Navigation` - 2 edges
9. `Report For Broad Architecture` - 2 edges
10. `Refresh Graph After Code` - 2 edges

## Surprising Connections (you probably didn't know these)
- `Native macOS Application` --conceptually_related_to--> `macOS ImageEditor Module`  [INFERRED]
  README.md → image-editor/README.md

## Communities (4 total, 2 thin omitted)

### Community 1 - "macOS Graph Navigation"
Cohesion: 0.33
Nodes (4): Graphify Explain, Graphify Path, Graphify Query, macOS Knowledge Graph

### Community 2 - "macOS Native Pilot"
Cohesion: 0.33
Nodes (5): AppKit Native Surfaces, macOS 14 Sonoma Minimum, D-MAC-001, Swift Language, SwiftUI Chrome

## Knowledge Gaps
- **8 isolated node(s):** `D-MAC-001`, `SwiftUI Chrome`, `AppKit Native Surfaces`, `Swift Language`, `macOS 14 Sonoma Minimum` (+3 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 10 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **2 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `macOS Knowledge Graph` connect `macOS Graph Navigation` to `macOS Agent Guidance`?**
  _High betweenness centrality (0.182) - this node is a cross-community bridge._
- **What connects `D-MAC-001`, `SwiftUI Chrome`, `AppKit Native Surfaces` to the rest of the system?**
  _8 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Why does `macOS Pilot Default` connect `macOS Native Pilot` to `macOS Native ImageEditor`?**
  _High betweenness centrality (0.132) - this node is a cross-community bridge._
- **Why does `macOS Scoped Agent Guidance` connect `macOS Agent Guidance` to `macOS Graph Navigation`?**
  _High betweenness centrality (0.092) - this node is a cross-community bridge._

## Build Coverage

- This is a documentation-only native platform stub. The application and ImageEditor implementation have not started.
- Every detected document was read completely; semantic source locations and per-file coverage were verified.
- EXTRACTED edges reflect explicit statements. The application-to-ImageEditor association is INFERRED from their shared platform ownership.
- Codex hook configuration is detected as code; it is not a product implementation.
- Host-agent token usage is unavailable; zero persisted counters are placeholders.

## Scoped Build Verification

- Independent scan root: `platforms/macos`. Semantic files were fully read, or reused only after source-content and extraction-prompt hash validation.
- Verified a foreground `graphify update .` preserves semantic nodes; code-only updates do not semantically re-extract prose. All scoped manifests are current.
- Host-agent token usage is unavailable; zero stored counters do not indicate zero semantic processing cost.
- Cite EXTRACTED versus INFERRED relationships; verify inferred links against source before making correctness or parity claims.
