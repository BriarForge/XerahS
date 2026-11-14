# Graph Report - product-contract  (2026-11-14)

## Corpus Check
- 98 files · ~168,244 words
- Verdict: corpus is large enough that graph structure adds value.
- Unclassified: 16 file(s) not represented in the graph (top: .feature 16)

## Summary
- 1276 nodes · 1565 edges · 55 communities (54 shown, 1 thin omitted)
- Extraction: 98% EXTRACTED · 2% INFERRED · 0% AMBIGUOUS · INFERRED: 24 edges (avg confidence: 0.9)
- Token cost: 0 input · 0 output

## Graph Freshness
- Built from commit: `a5232a44`
- Run `git rev-parse HEAD` and compare to check if the graph is stale.
- Run `graphify update .` after code changes (no API cost).

## Community Hubs (Navigation)
- REGION-CAPTURE-001 Interactive region capture
- CORE-PLATFORM-001 Native platform capability foundation
- EDITOR-ANNOTATIONS-001 Annotation tools
- Contract approval review — 2026-08-31
- Document
- POST-CAPTURE-ACTIONS-001 Post-capture actions
- MOBILE-PLATFORM-001 Native mobile applications
- SETTINGS-CATALOG-001 Application settings catalog
- EDITOR-UTILITIES-001 Image utility suite
- INTERFACE-CATALOG-001 Native and agent interfaces
- WORKFLOW-CATALOG-001 Workflow and action catalog
- Product Contract completion status
- kova-0.29.0.yaml
- EDITOR-OUTPUT-ACTIONS-001 Output and task actions
- COMPATIBILITY-CATALOG-001 Persisted-data compatibility
- EDITOR-CANVAS-001 Canvas and image operations
- items
- EDITOR-SETTINGS-001 Editor settings
- items
- items
- items
- items
- EDITOR-EFFECTS-001 Image effects
- APPROVAL-2026-08-31-NUGET-FRESHNESS.json
- APPROVAL-2026-08-31-PRODUCT-IDENTITY.json
- APPROVAL-2026-09-28-IMPLEMENTATION-READINESS.json
- APPROVAL-2026-09-28-MOBILE-TARGETS.json
- APPROVAL-2026-10-08-LINUX-HYPRLAND-TOOLKIT.json
- APPROVAL-2026-11-12-FILENAME-WIDE-SAMPLING.json
- Product Contract governance
- Filename deterministic acceptance vectors
- Product Contract manifest
- APPROVAL-2026-11-14-ES-EMPTY-SAVE-BASELINE.json
- Post-capture pipeline state machine
- KovaForge 0.29.0 — runtime observation of the pinned XANN sidecar behaviour
- values
- APPROVAL-2026-11-14-CORE-025-WATCH-PUBLICATION.json
- Product Contract reviews
- Conformance vector format
- backlog.json
- Deterministic filename generation scenarios
- routing
- values
- Native implementation readiness
- Product Contract
- capabilities
- values
- CORE-025: watch publication disposition and retention
- Schemas
- Decisions
- baseline-deltas.yaml
- EDITOR-CANVAS-001
- EDITOR-EFFECTS-001
- Waivers

## God Nodes (most connected - your core abstractions)
1. `Filename deterministic acceptance vectors` - 55 edges
2. `REGION-CAPTURE-001 Interactive region capture` - 53 edges
3. `Document` - 51 edges
4. `CORE-PLATFORM-001 Native platform capability foundation` - 45 edges
5. `EDITOR-ANNOTATIONS-001 Annotation tools` - 44 edges
6. `POST-CAPTURE-ACTIONS-001 Post-capture actions` - 44 edges
7. `FILENAME-GENERATION-001 Filename generation` - 41 edges
8. `MOBILE-PLATFORM-001 Native mobile applications` - 41 edges
9. `SETTINGS-CATALOG-001 Application settings catalog` - 39 edges
10. `EDITOR-UTILITIES-001 Image utility suite` - 37 edges

## Surprising Connections (you probably didn't know these)
- `FILENAME-GENERATION-001` --references--> `FILENAME-GENERATION-001 Filename generation`  [EXTRACTED]
  manifest.yaml → capabilities/FILENAME-GENERATION-001/SPEC.md
- `IEEE 754 binary64` --semantically_similar_to--> `IEEE 754 binary64`  [INFERRED] [semantically similar]
  VECTORS.md → capabilities/EDITOR-SESSION-001/SPEC.md
- `test-vectors.json` --semantically_similar_to--> `test-vectors.json`  [INFERRED] [semantically similar]
  VECTORS.md → capabilities/EDITOR-SESSION-001/README.md
- `Mike approval of filename wider sampling` --references--> `FILENAME-GENERATION-001 Filename generation`  [EXTRACTED]
  reviews/APPROVAL-2026-11-12-FILENAME-WIDE-SAMPLING.json → capabilities/FILENAME-GENERATION-001/SPEC.md
- `Product Contract` --conceptually_related_to--> `REGION-CAPTURE-001 Interactive region capture`  [INFERRED]
  glossary.md → capabilities/REGION-CAPTURE-001/SPEC.md

## Import Cycles
- None detected.

## Communities (55 total, 1 thin omitted)

### Community 0 - "REGION-CAPTURE-001 Interactive region capture"
Cohesion: 0.05
Nodes (62): REGION-CAPTURE-001, Atomic last-region persistence, Capture result topology snapshot, Cross-monitor source-pixel composition, Desktop physical coordinate space, Documented HDR wide-gamut conversion, DXGI WinRT Win32 capture, Exactly-once terminal capture result (+54 more)

### Community 1 - "CORE-PLATFORM-001 Native platform capability foundation"
Cohesion: 0.04
Nodes (11): CORE-PLATFORM-001, experimental-mobile, native invariants, com.xerahs.app, CORE-PLATFORM-001 Native platform capability foundation, GPLv3, linux-editions.json, MOBILE-PLATFORM-001 (+3 more)

### Community 2 - "EDITOR-ANNOTATIONS-001 Annotation tools"
Cohesion: 0.05
Nodes (25): annotation-tools.json, EDITOR-ANNOTATIONS-001, kova-0.29.0-census.json, annotation-tools.json, Arrow, Blur, Crop, Cursor (+17 more)

### Community 3 - "Contract approval review — 2026-08-31"
Cohesion: 0.05
Nodes (42): Accepted setting dispositions, Human census closure approval, ImageEditor static inventory, Independent user-journey review, KovaForge 0.29.0 census, KovaForge 0.29.0 census provenance, Open baseline census, Sanitized compatibility fixtures (+34 more)

### Community 4 - "Document"
Cohesion: 0.04
Nodes (16): .xann, annotation-document.schema.json, EDITOR-SESSION-001, test-vectors.json, #AARRGGBB, annotation-document.schema.json, COMPAT-XANN-001, Dirty state (+8 more)

### Community 5 - "POST-CAPTURE-ACTIONS-001 Post-capture actions"
Cohesion: 0.07
Nodes (41): MOB-013, Process termination recovery, actions.json normative catalogue, Artifacts, Before-upload decline, Immutable selected action settings snapshot, Independent action failure continuation, Interrupted unknown remote outcome (+33 more)

### Community 6 - "MOBILE-PLATFORM-001 Native mobile applications"
Cohesion: 0.08
Nodes (39): MOBILE-PLATFORM-001, Android 10 and iOS 17 baseline, Android intents iOS App Intents Shortcuts, Android MediaProjection consent, GPLv3 iOS licensing review, iOS screenshot and photo picker source, Keystore and this-device-only Keychain, MediaCodec and ReplayKit recording (+31 more)

### Community 7 - "SETTINGS-CATALOG-001 Application settings catalog"
Cohesion: 0.08
Nodes (39): SETTINGS-CATALOG-001, Adaptive window geometry, All-platform settings activation evidence, Approved setting field dispositions, Atomic crash-safe settings unknown round-trip, Consent-preserving settings, Deterministic scoped preference precedence, Explicit legacy plaintext secret migration (+31 more)

### Community 8 - "EDITOR-UTILITIES-001 Image utility suite"
Cohesion: 0.05
Nodes (18): EDITOR-UTILITIES-001, utility-catalog.json, Background Removal, CRC32, EDITOR-UTILITIES-001 Image utility suite, Hash Checker, Icon Converter, Image Comparison (+10 more)

### Community 9 - "INTERFACE-CATALOG-001 Native and agent interfaces"
Cohesion: 0.08
Nodes (34): INTERFACE-CATALOG-001, Declarative provider field schemas, Explicit cancellable connection tests, IFCAT-001, IFCAT-002, IFCAT-003, IFCAT-004, IFCAT-005 (+26 more)

### Community 10 - "WORKFLOW-CATALOG-001 Workflow and action catalog"
Cohesion: 0.08
Nodes (34): WORKFLOW-CATALOG-001, All-platform workflow activation evidence, Approved action ordering graph, Bounded idempotent workflow retry, Canonical paths destructive authority, Consistent redacted terminal observations, Cooperative idempotent cancellation, Deterministic resource arbitration (+26 more)

### Community 11 - "Product Contract completion status"
Cohesion: 0.06
Nodes (33): 174, 1,592, 436, 922, Baseline census, Product Contract completion status, kova-0.29.0, Manifest lifecycle (+25 more)

### Community 12 - "kova-0.29.0.yaml"
Cohesion: 0.06
Nodes (31): application_version, branch_when_pinned, census_status, closure_requirements, commit, authority, classification, path (+23 more)

### Community 13 - "EDITOR-OUTPUT-ACTIONS-001 Output and task actions"
Cohesion: 0.06
Nodes (16): EDITOR-OUTPUT-ACTIONS-001, host-service conformance, Cancel, Continue, Copy Image, EDITOR-OUTPUT-ACTIONS-001 Output and task actions, format-disposition.json, Host callbacks (+8 more)

### Community 14 - "COMPATIBILITY-CATALOG-001 Persisted-data compatibility"
Cohesion: 0.07
Nodes (10): COMPATIBILITY-CATALOG-001, legacy configuration, runtime-observation, COMPATIBILITY-CATALOG-001 Persisted-data compatibility, explicit consent, ImageEditor, MCP, SQLite (+2 more)

### Community 15 - "EDITOR-CANVAS-001 Canvas and image operations"
Cohesion: 0.08
Nodes (10): Auto Crop, Crop Image, EDITOR-CANVAS-001 Canvas and image operations, Flatten, Image comparison, Insert Image, New Image, Open (+2 more)

### Community 16 - "items"
Cohesion: 0.08
Nodes (25): baseline, linux, macos, windows, items, baseline_commit, classification, compatibility (+17 more)

### Community 17 - "EDITOR-SETTINGS-001 Editor settings"
Cohesion: 0.08
Nodes (8): EDITOR-SETTINGS-001, settings ledger, atomic, EDITOR-SETTINGS-001 Editor settings, Recent image paths, shortcut-catalog.json, Toolbar, unknown compatible

### Community 18 - "items"
Cohesion: 0.08
Nodes (23): baseline, linux, macos, windows, items, baseline_commit, classification, compatibility (+15 more)

### Community 19 - "items"
Cohesion: 0.08
Nodes (23): baseline, linux, macos, windows, items, baseline_commit, classification, compatibility (+15 more)

### Community 20 - "items"
Cohesion: 0.08
Nodes (23): baseline, linux, macos, windows, items, baseline_commit, classification, compatibility (+15 more)

### Community 21 - "items"
Cohesion: 0.08
Nodes (23): baseline, linux, macos, windows, items, baseline_commit, classification, compatibility (+15 more)

### Community 22 - "EDITOR-EFFECTS-001 Image effects"
Cohesion: 0.09
Nodes (5): 13, 219, 232, 963, EDITOR-EFFECTS-001 Image effects

### Community 23 - "APPROVAL-2026-08-31-NUGET-FRESHNESS.json"
Cohesion: 0.09
Nodes (21): authority, identity, role, decision, decision_id, effects, activation_authorized, census_closed (+13 more)

### Community 24 - "APPROVAL-2026-08-31-PRODUCT-IDENTITY.json"
Cohesion: 0.09
Nodes (21): authority, identity, role, decision, decision_id, effects, activation_authorized, census_closed (+13 more)

### Community 25 - "APPROVAL-2026-09-28-IMPLEMENTATION-READINESS.json"
Cohesion: 0.09
Nodes (21): authority, identity, role, decision, decision_id, effects, activation_authorized, census_closed (+13 more)

### Community 26 - "APPROVAL-2026-09-28-MOBILE-TARGETS.json"
Cohesion: 0.09
Nodes (21): authority, identity, role, decision, decision_id, effects, activation_authorized, census_closed (+13 more)

### Community 27 - "APPROVAL-2026-10-08-LINUX-HYPRLAND-TOOLKIT.json"
Cohesion: 0.09
Nodes (21): authority, identity, role, decision, decision_id, effects, activation_authorized, census_closed (+13 more)

### Community 28 - "APPROVAL-2026-11-12-FILENAME-WIDE-SAMPLING.json"
Cohesion: 0.09
Nodes (21): authority, identity, role, decision, decision_id, effects, activation_authorized, census_closed (+13 more)

### Community 29 - "Product Contract governance"
Cohesion: 0.10
Nodes (16): Capability manifest, Contract backlog tracker, Contract-first change workflow, Contract linter, Graphify explain, Graph-first navigation, Graphify path, Graphify query (+8 more)

### Community 30 - "Filename deterministic acceptance vectors"
Cohesion: 0.05
Nodes (95): FILENAME-GENERATION-001, Atomic name reservation counter, Cryptographic rejection sampling, Deterministic non-committing preview, FILENAME-GENERATION-001 Filename generation, FN-001, FN-002, FN-003 (+87 more)

### Community 31 - "Product Contract manifest"
Cohesion: 0.11
Nodes (18): COMPATIBILITY-CATALOG-001, Contract version 0.3.1, CORE-PLATFORM-001, EDITOR-ANNOTATIONS-001, EDITOR-CANVAS-001, EDITOR-EFFECTS-001, EDITOR-OUTPUT-ACTIONS-001, EDITOR-SESSION-001 (+10 more)

### Community 32 - "APPROVAL-2026-11-14-ES-EMPTY-SAVE-BASELINE.json"
Cohesion: 0.09
Nodes (21): authority, identity, role, decision, decision_id, effects, activation_authorized, census_closed (+13 more)

### Community 33 - "Post-capture pipeline state machine"
Cohesion: 0.23
Nodes (17): POST-CAPTURE-ACTIONS-001, Action cancelled, Action failed, Action pending, Action running, Action skipped, Action succeeded, Append-only linked retry (+9 more)

### Community 34 - "KovaForge 0.29.0 — runtime observation of the pinned XANN sidecar behaviour"
Cohesion: 0.12
Nodes (14): ES-014/030: empty save with a baseline companion, 1.1 Observation host, 1. Provenance of the observed baseline, 2. Exact commands, 3.1 `GetDefaultSidecarPath("d/shot.png")`, 3.2 Save with exactly one annotation, 3.3 Save with zero annotations (the empty-save case), 3.4 Deletion is reported accurately and is idempotent (+6 more)

### Community 35 - "values"
Cohesion: 0.15
Nodes (13): values, application-shell, automation-integration, capture, configuration-security, diagnostics-recovery, distribution-quality, history-indexing (+5 more)

### Community 36 - "APPROVAL-2026-11-14-CORE-025-WATCH-PUBLICATION.json"
Cohesion: 0.09
Nodes (21): authority, identity, role, decision, decision_id, effects, activation_authorized, census_closed (+13 more)

### Community 37 - "Product Contract reviews"
Cohesion: 0.15
Nodes (13): Minimal-width big-endian random-file sampling, Mike approval of filename wider sampling, Remaining platform conformance and qualification gates, Current manifest version approval scope, Filename wider sampling approval 0.2.1, Human approval decision evidence, Hyprland Qt LayerShellQt approval 0.3.1, Implementation-readiness approval 0.2.0 (+5 more)

### Community 38 - "Conformance vector format"
Cohesion: 0.20
Nodes (10): CONTRACT-EVIDENCE-001, Conformance vector format, exact, IEEE 754 binary64, ROOT-PROHIBIT-001, ROOT-REVIEW-001, schemas/test-vectors.schema.json, signed 64-bit (+2 more)

### Community 39 - "backlog.json"
Cohesion: 0.17
Nodes (11): baseline, completion_definition, objective, packages, $schema, schema_version, tracker_version, waves (+3 more)

### Community 40 - "Deterministic filename generation scenarios"
Cohesion: 0.17
Nodes (12): An incomplete wider random-file draw preserves the counter, Counter advances only once for multiple counter tokens, Date, time, dimensions, and counter expand from one context, Deterministic filename generation scenarios, Every eligible random-file line remains reachable above 256 entries, Metadata cannot create directories in path mode, Path traversal is rejected, Portable invalid characters are deterministic (+4 more)

### Community 41 - "routing"
Cohesion: 0.22
Nodes (9): field, routing, capability, settings, workflow, default, field, default (+1 more)

### Community 42 - "values"
Cohesion: 0.22
Nodes (9): field, values, compatibility, automation, configuration, history, image-effects, plugin (+1 more)

### Community 43 - "Native implementation readiness"
Cohesion: 0.22
Nodes (9): Contract 0.3.1 readiness, Headless test seams each implementation provides, MOB-018 iOS licensing gate, Native implementation readiness, Open gates that do not block starting, Rule for ambiguity found during coding, Start order, Status by target (+1 more)

### Community 44 - "Product Contract"
Cohesion: 0.22
Nodes (9): BXIP001, D-CON-001, Product Contract, parity/, READINESS.md, reference-baselines/, tools/baseline-census/census.py, tools/contract-backlog/tracker.py (+1 more)

### Community 45 - "capabilities"
Cohesion: 0.22
Nodes (8): capabilities, id, path, requirements, status, version, contract_version, status

### Community 46 - "values"
Cohesion: 0.29
Nodes (7): field, values, interface, cli, destination, gui, mcp

### Community 48 - "CORE-025: watch publication disposition and retention"
Cohesion: 0.20
Nodes (9): Binding amendment, 2026-11-14, CORE-025: watch publication disposition and retention, Decision A: accepted uploads without a usable result, Decision B: parent identity retention and store format, Decision C: retry session and notices, Durable intent and local file effects, Implementation and qualification, Problem (+1 more)

### Community 53 - "Schemas"
Cohesion: 0.40
Nodes (5): approval-record.schema.json, backlog.schema.json, manifest.schema.json, parity-ledger.schema.json, Schemas

### Community 54 - "Decisions"
Cohesion: 0.50
Nodes (4): BXIP001 architecture decisions, Contract decisions, D-AGT-001 depth exceptions, Decisions

### Community 55 - "baseline-deltas.yaml"
Cohesion: 0.50
Nodes (3): current_baseline, schema_version, transitions

### Community 56 - "EDITOR-CANVAS-001"
Cohesion: 0.67
Nodes (3): comparison, EDITOR-CANVAS-001, flattening

### Community 57 - "EDITOR-EFFECTS-001"
Cohesion: 0.67
Nodes (3): EDITOR-EFFECTS-001, effect-catalog-selection.json, effect-catalog-selection.json

### Community 58 - "Waivers"
Cohesion: 0.67
Nodes (3): D-REL-001 release waiver policy, Time-bound platform waiver, Waivers

## Knowledge Gaps
- **282 isolated node(s):** `Capture result topology snapshot`, `Desktop physical coordinate space`, `DXGI WinRT Win32 capture`, `Half-open physical selection rectangle`, `IEEE754 floor epsilon mapping` (+277 more)
  These have ≤1 connection - possible missing edges or undocumented components. (Counts symbols only; 806 node(s) total have ≤1 connection when file, concept and rationale nodes are included.)
- **1 thin communities (<3 nodes) omitted from report** — run `graphify query` to explore isolated nodes.

## Suggested Questions
_Questions this graph is uniquely positioned to answer:_

- **Why does `Product Contract` connect `INTERFACE-CATALOG-001 Native and agent interfaces` to `REGION-CAPTURE-001 Interactive region capture`, `POST-CAPTURE-ACTIONS-001 Post-capture actions`, `MOBILE-PLATFORM-001 Native mobile applications`, `SETTINGS-CATALOG-001 Application settings catalog`, `WORKFLOW-CATALOG-001 Workflow and action catalog`, `Filename deterministic acceptance vectors`?**
  _High betweenness centrality (0.083) - this node is a cross-community bridge._
- **What connects `Capture result topology snapshot`, `Desktop physical coordinate space`, `DXGI WinRT Win32 capture` to the rest of the system?**
  _282 weakly-connected nodes found - possible documentation gaps or missing edges._
- **Should `REGION-CAPTURE-001 Interactive region capture` be split into smaller, more focused modules?**
  _Cohesion score 0.05129561078794289 - nodes in this community are weakly interconnected._
- **Why does `Product Contract` connect `Product Contract` to `EDITOR-ANNOTATIONS-001 Annotation tools`, `Contract approval review — 2026-08-31`, `Document`, `Conformance vector format`, `backlog.json`, `EDITOR-UTILITIES-001 Image utility suite`, `Product Contract completion status`, `Native implementation readiness`, `EDITOR-OUTPUT-ACTIONS-001 Output and task actions`, `EDITOR-CANVAS-001 Canvas and image operations`, `EDITOR-SETTINGS-001 Editor settings`, `EDITOR-EFFECTS-001 Image effects`?**
  _High betweenness centrality (0.065) - this node is a cross-community bridge._
- **Should `CORE-PLATFORM-001 Native platform capability foundation` be split into smaller, more focused modules?**
  _Cohesion score 0.041666666666666664 - nodes in this community are weakly interconnected._
- **Why does `FILENAME-GENERATION-001 Filename generation` connect `Filename deterministic acceptance vectors` to `INTERFACE-CATALOG-001 Native and agent interfaces`, `Product Contract reviews`, `Product Contract manifest`?**
  _High betweenness centrality (0.055) - this node is a cross-community bridge._
- **Should `EDITOR-ANNOTATIONS-001 Annotation tools` be split into smaller, more focused modules?**
  _Cohesion score 0.046511627906976744 - nodes in this community are weakly interconnected._