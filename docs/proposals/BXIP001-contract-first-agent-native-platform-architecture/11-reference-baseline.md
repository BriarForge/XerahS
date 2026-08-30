# 20. Reference Baseline and Capability Census

## 20.1 Pinned source snapshot

The initial full-parity baseline is the clean KovaForge XerahS desktop checkout observed on 2026-08-30:

| Field | Pinned value |
|---|---|
| Repository | [KovaForge/XerahS](https://github.com/KovaForge/XerahS) |
| Local inspection path | `C:\Users\Public\source\repos\KovaForge\XerahS` |
| Branch | `develop` |
| Commit | `5c7e36dea77ab131fe0f5e2101e5d578ccde0306` |
| Application version | `0.29.0` |
| Working tree when pinned | Clean and synchronized with `origin/develop` |
| ShareX.ImageEditor reference | `651b1d8de4bc1f874790870560314670cd038684` |
| ShareX.VideoEditor reference | `0482ee322a3086d5af135aa9b54a37803241cb0d` |

The commit and submodule pointers, not the local path or a moving branch name, identify the reproducible baseline. The local path is recorded only so agents on this workstation inspect the intended checkout.

The Product Contract remains product truth. The pinned source is the authoritative **discovery baseline** for determining which desktop capabilities, settings, workflows, data formats, integrations, and failure behaviors must receive a contract disposition. Existing code does not become normative merely because it exists.

## 20.2 Full functional parity

For BXIP001, **full functional parity** means:

1. Every user-reachable desktop capability in the pinned baseline is represented in the capability ledger.
2. Every persisted setting, workflow choice, task action, command, integration, and supported data format is represented in a settings, workflow, interface, or compatibility ledger.
3. Every ledger row has contract requirements and a disposition for Windows, macOS, and Linux.
4. Every Required or Equivalent row is implemented and backed by conformance plus native evidence.
5. Known defects may be corrected rather than reproduced, but the correction is explicit and tested.
6. Platform-native interaction may differ, but the contracted user outcome remains equivalent.
7. No baseline feature disappears because an agent failed to discover, understand, or schedule it.

Full parity is behavioral and operational, not structural. BriarForge/XerahS is not a fork and does not reproduce KovaForge namespaces, project layout, Avalonia views, service locators, or internal class boundaries.

## 20.3 Baseline scope

The parity mandate includes the KovaForge desktop product and its shipped companion tooling:

- Windows, macOS, and Linux desktop application behavior
- GUI surfaces, tray behavior, hotkeys, lifecycle, onboarding, settings, workflows, and task execution
- Screen capture, region capture, scrolling capture, auto capture, and recording
- ImageEditor, annotations, image effects, re-editable projects, and export
- VideoEditor and media utilities reachable from the desktop product
- Uploaders, custom uploaders, URL shorteners, destination instances, authentication, and plugin extensibility outcomes
- History, indexing, thumbnails, search, cleanup, and task-result persistence
- CLI, MCP, assistant, Send To, watch-folder daemon, and automation behavior
- Configuration import, migration, backup, secrets, logs, diagnostics, recovery, and compatibility formats
- Desktop packaging, installation, startup, file associations, update behavior, and uninstall safety
- Accessibility, localization, performance, permissions, privacy, and security behavior

`src/mobile-experimental/` is explicitly outside BXIP001 because the approved target set is Windows, macOS, and Linux desktop. It SHALL still appear in the census with disposition `OutOfScope-BXIP001` so it is not silently forgotten; a mobile-native program requires a separate proposal.

Dead code that is not user-reachable is not automatically a parity requirement. Deprecated but still loadable settings and formats remain compatibility requirements until explicitly retired.

## 20.4 Required baseline artifacts

Implementation SHALL create and maintain:

```text
product-contract/
  reference-baselines/
    kova-0.29.0.yaml
    provenance/
  parity/
    capability-ledger.yaml
    settings-ledger.yaml
    workflow-ledger.yaml
    interface-ledger.yaml
    compatibility-ledger.yaml
    baseline-deltas.yaml
```

`kova-0.29.0.yaml` records the values in section 20.1, the supported desktop scope, submodule pointers, inventory-tool versions, and hashes of generated ledger inputs.

Every ledger row SHALL contain at least:

```yaml
id: CAPTURE-REGION-001
domain: capture
user_outcome: Select and capture an arbitrary screen region
source_evidence:
  - path: src/desktop/app/XerahS.RegionCapture/...
  - symbol: WorkflowType.RectangleRegion
baseline_commit: 5c7e36dea77ab131fe0f5e2101e5d578ccde0306
contract: product-contract/capabilities/capture/REGION-CAPTURE-001
platforms:
  windows: required
  macos: required
  linux: required
evidence:
  windows: []
  macos: []
  linux: []
compatibility: []
status: inventoried
```

Allowed lifecycle states are:

`discovered -> inventoried -> contracted -> implemented -> conformant -> release-verified`

`Not implemented`, an empty platform field, or an unlinked source-evidence field is never a terminal state.

## 20.5 Census procedure

The census SHALL be generated from multiple independent evidence sources so no single code-navigation method defines scope:

1. Project and package inventory, including desktop apps, platform projects, tools, first-party plugins, and pinned submodules.
2. Public enums and registries for workflow types, capture modes, after-capture tasks, after-upload tasks, destinations, editor tools, effects, and CLI commands.
3. Settings models, defaults, validators, serializers, migrations, backup paths, and secret fields.
4. GUI routes, views, dialogs, menus, tray actions, context menus, keyboard shortcuts, accessibility actions, and error surfaces.
5. CLI, MCP, assistant, daemon, Send To, plugin, and automation entry points.
6. File formats, schemas, extensions, database tables, importers, exporters, and network protocols.
7. Tests, current documentation, architecture maps, XIPs, known-issue records, and release notes.
8. Runtime observation of representative workflows on every platform where the KovaForge baseline currently runs.

Graph or AST extraction accelerates discovery but cannot replace UI, settings, format, and runtime inspection. The census tooling SHALL emit its version, input commit, and unresolved symbols.

Two independent reviews are required before the census is declared closed:

- a source-structure review that traces projects, registries, and data models
- a user-journey review that traces every visible action from launch through result and recovery

## 20.6 Classification and defects

Every discovered behavior receives one of these product-level classifications:

| Classification | Meaning |
|---|---|
| Preserve | Same user outcome and compatibility are required |
| Native equivalent | Outcome is required through platform-idiomatic interaction |
| Correct defect | Baseline behavior is a verified bug; desired behavior is contracted and regression-tested |
| Retire intentionally | Product owner approves removal with rationale, migration, and user impact |
| Not applicable | The behavior has no meaning on a platform; product owner approves |
| OutOfScope-BXIP001 | Explicitly assigned to another approved program |

`Retire intentionally`, `Not applicable`, and `OutOfScope-BXIP001` require human product-owner approval. An implementation agent cannot use them to reduce workload.

## 20.7 Moving-baseline policy

The pinned baseline never advances implicitly. Updating it requires:

1. A human-approved new KovaForge commit.
2. A generated diff between old and new projects, settings, registries, commands, schemas, and user surfaces.
3. New or changed ledger rows for every delta.
4. Contract version impact analysis.
5. Updated fixtures, provenance, and submodule pointers.
6. A recorded baseline transition in `baseline-deltas.yaml`.

This prevents an endless moving target while allowing deliberate synchronization with later KovaForge releases.

## 20.8 Baseline completion gate

Baseline discovery is complete only when:

- all inventory sources in section 20.5 have evidence
- every discovered item maps to a stable ledger ID
- no unresolved project, setting, workflow enum value, command, view, plugin, schema, or user-reachable tool remains
- the product owner signs the census closure
- CI can regenerate the inventory and detect an unexplained difference

Implementation may begin by domain before census closure, but the first parity release cannot be declared until this gate passes.
